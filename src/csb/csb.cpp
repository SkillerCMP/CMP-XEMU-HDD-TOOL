// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/csb.hpp"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace xhc::csb {
namespace {
const Node* child(const Node& parent, const std::string& name) {
    const auto key = ascii_lower(name);
    for (const auto& n : parent.children)
        if (ascii_lower(n.name) == key)
            return &n;
    return nullptr;
}
Node* child(Node& parent, const std::string& name) {
    return const_cast<Node*>(child(static_cast<const Node&>(parent), name));
}
const Node* music_node(const Volume& vol) {
    auto* n = child(vol.root, "TDATA");
    if (n) {
        require(n->directory(), "E:/TDATA is not a directory.");
        n = child(*n, "fffe0000");
    }
    if (n) {
        require(n->directory(), "E:/TDATA/fffe0000 is not a directory.");
        n = child(*n, "music");
    }
    if (n)
        require(n->directory(), "The music path is not a directory.");
    return n;
}
const Volume& e_volume(const std::vector<Volume>& volumes) {
    for (const auto& v : volumes)
        if (v.layout.letter == 'E')
            return v;
    throw Error("E: was not found in the validated disk.");
}
bool parse_hex(const std::string& text, size_t digits, uint32_t& value) {
    if (text.size() != digits)
        return false;
    value = 0;
    for (char c : text) {
        unsigned d = c >= '0' && c <= '9'   ? unsigned(c - '0')
                     : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10)
                     : c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10)
                                            : 16;
        if (d > 15)
            return false;
        value = (value << 4) | d;
    }
    return true;
}
Bytes read_node(const Read& read, const Volume& vol, const Node& n, Context& ctx) {
    require(!n.directory() && n.length <= 8 * 1024 * 1024, "Unexpected ST.DB type or size.");
    Bytes bytes;
    bytes.reserve(n.length);
    stream_node(read, vol, n, [&](const uint8_t* p, size_t count) { bytes.insert(bytes.end(), p, p + count); }, ctx);
    return bytes;
}
uint64_t folder_bytes(const Node& node) {
    uint64_t total = node.directory() ? 0 : node.length;
    for (const auto& c : node.children)
        total += folder_bytes(c);
    return total;
}
void warn(Catalog& catalog, const std::string& text, bool fatal = false) {
    if (!catalog.warning.empty())
        catalog.warning += "\n";
    catalog.warning += text;
    if (fatal)
        catalog.writable = false;
}
std::string fingerprint(const std::vector<Volume>& vols, const Bytes& system) {
    auto inventory = inventory_json(vols);
    Sha256 sha;
    sha.add(system.data(), system.size());
    sha.add(inventory.data(), inventory.size());
    return sha.finish();
}
uint32_t next_serial(const Catalog& c, const Edit& e) {
    auto used = c.reserved_serials;
    for (const auto& r : e.rows)
        used.insert(r.track.song_id);
    uint32_t n = c.metadata.next_song_id;
    while (n <= 0xFFFF && used.count(static_cast<uint16_t>(n)))
        ++n;
    require(n <= 0xFFFF, "The global 16-bit song serial is exhausted; existing IDs will not be reused.");
    return n;
}
std::array<uint16_t, 6> timestamps() {
    std::time_t now = std::time(nullptr);
    std::tm t{};
#ifdef _WIN32
    gmtime_s(&t, &now);
#else
    gmtime_r(&now, &t);
#endif
    const int year = std::clamp(t.tm_year + 1900, 1980, 2107);
    const uint16_t time = uint16_t((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
    const uint16_t date = uint16_t(((year - 1980) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    return {{time, date, time, date, time, date}};
}
Node& directory(Node& parent, const std::string& name) {
    if (auto* n = child(parent, name)) {
        require(n->directory(), "A file occupies directory " + name);
        return *n;
    }
    Node n;
    n.name = name;
    n.attributes = 0x10;
    n.times = timestamps();
    parent.children.push_back(std::move(n));
    return parent.children.back();
}
Node staged_file(const fs::path& path, const std::string& name, Context& ctx) {
    File f(absolute_safe(path, true));
    require(f.size() <= std::numeric_limits<uint32_t>::max(), "Audio exceeds the FATX file size limit.");
    Node n;
    n.name = name;
    n.attributes = 0x20;
    n.times = timestamps();
    n.length = static_cast<uint32_t>(f.size());
    n.digest = hash_file(f, ctx);
    n.replacement = path;
    f.stable();
    return n;
}
void backup_node(const Read& read, const Volume& vol, const Node& n, const fs::path& output, Context& ctx) {
    ctx.check();
    if (n.directory()) {
        require(fs::create_directory(output), "Cannot create soundtrack backup directory.");
        for (const auto& c : n.children) {
            validate_host_component(c.name);
            backup_node(read, vol, c, output / fs::u8path(c.name), ctx);
        }
        sync_directory(output);
    } else {
        {
            File out(output, true, n.length);
            uint64_t pos = 0;
            Sha256 sha;
            stream_node(
                read, vol, n,
                [&](const uint8_t* p, size_t count) {
                    out.write_sparse(pos, p, count);
                    sha.add(p, count);
                    pos += count;
                },
                ctx);
            out.flush();
            require(sha.finish() == n.digest, "Source changed while making the backup.");
        }
        File verify(output);
        require(hash_file(verify, ctx) == n.digest, "Fresh backup hash verification failed.");
    }
}
void same_records(const std::vector<StDbSoundtrack>& a, const std::vector<StDbSoundtrack>& b) {
    require(a.size() == b.size(), "ST.DB verification: soundtrack count changed.");
    for (size_t i = 0; i < a.size(); ++i) {
        require(a[i].soundtrack_id == b[i].soundtrack_id && a[i].name == b[i].name &&
                    a[i].tracks.size() == b[i].tracks.size(),
                "ST.DB verification: soundtrack metadata differs.");
        for (size_t j = 0; j < a[i].tracks.size(); ++j) {
            const auto& x = a[i].tracks[j];
            const auto& y = b[i].tracks[j];
            require(x.song_id == y.song_id && x.duration_ms == y.duration_ms && x.title == y.title,
                    "ST.DB verification: song ID/title/duration differs (names are never silently truncated).");
        }
    }
}
void independent_paths(const fs::path& source, const fs::path& destination, const fs::path& parent) {
    require(!is_within(destination, source) && !is_within(source, destination),
            "CSB source and output must be independent.");
    if (!fs::is_directory(source))
        return;
    for (auto p = parent; !p.empty();) {
        std::error_code ec;
        bool same = fs::equivalent(p, source, ec);
        require(!ec && !same, "Backups/output must not be inside the source Folder-HDD, including aliases.");
        auto next = p.parent_path();
        if (next == p)
            break;
        p = next;
    }
}
void validate_edit(const Catalog& c, const Edit& e) {
    require(c.writable, "This catalog is read-only:\n" + c.warning);
    auto it = std::find_if(c.soundtracks.begin(), c.soundtracks.end(),
                           [&](const auto& s) { return s.record.soundtrack_id == e.id; });
    if (e.existing)
        require(it != c.soundtracks.end() && !it->orphan, "Selected soundtrack no longer exists or is an orphan.");
    else {
        require(it == c.soundtracks.end() && !e.delete_soundtrack, "New soundtrack ID collides with existing data.");
        require(e.id >= c.metadata.next_soundtrack_id,
                "A new soundtrack must honor the database-wide next soundtrack ID.");
        require(std::count_if(c.soundtracks.begin(), c.soundtracks.end(), [](const auto& s) { return !s.orphan; }) <
                    100,
                "The database already has 100 soundtracks.");
    }
    if (e.delete_soundtrack)
        return;
    require(!e.name.empty(), "A soundtrack name is required.");
    validate_name(e.name, 63);
    require(!e.rows.empty() && e.rows.size() <= 504,
            "Each soundtrack needs 1 to 504 songs; use Delete Soundtrack to remove it.");
    std::set<uint16_t> used;
    for (const auto& r : e.rows) {
        validate_name(r.track.title, 31);
        require(used.insert(r.track.song_id).second, "Duplicate pending song ID.");
        if (r.retained()) {
            require(e.existing, "A new soundtrack cannot retain an unknown HDD song.");
            const auto& songs = it->record.tracks;
            auto song =
                std::find_if(songs.begin(), songs.end(), [&](const auto& s) { return s.song_id == r.track.song_id; });
            require(song != songs.end() && song->duration_ms == r.track.duration_ms,
                    "Retained song does not match the loaded source.");
        } else {
            require(r.track.song_id >= c.metadata.next_song_id && !c.reserved_serials.count(r.track.song_id),
                    "A new track must use an unused database-wide song serial.");
            absolute_safe(r.local_file, true);
        }
    }
}
void apply_edit(std::vector<Volume>& vols, const Read& read, const Catalog& catalog, const Edit& edit,
                const fs::path& backup, const fs::path& work, const fs::path& ffmpeg_request, Context& ctx) {
    auto& vol = const_cast<Volume&>(e_volume(vols));
    const auto* old_music = music_node(vol);
    const std::string folder = XemuCsb::XboxSoundtrackFolderName(edit.id);
    const auto* old_folder = old_music ? child(*old_music, folder) : nullptr;
    const auto* old_db = old_music ? child(*old_music, "ST.DB") : nullptr;
    const std::string db_name = old_db ? old_db->name : "ST.DB";
    ctx.say("Backing up the existing database and affected soundtrack to: " + utf8(backup));
    if (old_db)
        backup_node(read, vol, *old_db, backup / "database.bak", ctx);
    if (old_folder)
        backup_node(read, vol, *old_folder, backup / folder, ctx);
    write_text_new(backup / "original-files.json", inventory_json({vol}));
    write_text_new(
        backup / "backup-verified.txt",
        "Original source retained. Every database/selected-folder file that existed has been freshly hash-verified; see the original inventory.\n");
    sync_directory(backup);
    std::vector<StDbSoundtrack> records;
    for (const auto& s : catalog.soundtracks)
        if (!s.orphan)
            records.push_back(s.record);
    auto rec = std::find_if(records.begin(), records.end(), [&](const auto& s) { return s.soundtrack_id == edit.id; });
    Node next_folder;
    if (old_folder) {
        require(old_folder->directory(), "Soundtrack path is not a folder.");
        next_folder = *old_folder;
    } else {
        next_folder.name = folder;
        next_folder.attributes = 0x10;
        next_folder.times = timestamps();
    }
    if (edit.delete_soundtrack) {
        records.erase(rec);
    } else {
        // Remove only database-listed songs from the working selected folder; preserve unknown files.
        if (rec != records.end()) {
            for (const auto& t : rec->tracks) {
                const auto file = ascii_lower(XemuCsb::XboxSoundtrackFileName(edit.id, t.song_id));
                const bool retained = std::any_of(edit.rows.begin(), edit.rows.end(), [&](const Row& r) {
                    return r.retained() && r.track.song_id == t.song_id;
                });
                if (!retained)
                    next_folder.children.erase(
                        std::remove_if(next_folder.children.begin(), next_folder.children.end(),
                                       [&](const Node& n) { return ascii_lower(n.name) == file; }),
                        next_folder.children.end());
            }
        }
        StDbSoundtrack next;
        next.soundtrack_id = edit.id;
        next.name = edit.name;
        fs::path ffmpeg;
        for (size_t i = 0; i < edit.rows.size(); ++i) {
            ctx.check();
            const auto& row = edit.rows[i];
            StDbTrack track = row.track;
            if (!row.retained()) {
                if (ffmpeg.empty())
                    ffmpeg = find_ffmpeg(ffmpeg_request);
                const auto input = absolute_safe(row.local_file, true);
                File input_guard(input);
                require(input_guard.size() > 0, "An imported audio file is empty.");
                const auto audio = work / ("audio-" + std::to_string(i) + ".wma");
                ctx.say("Converting track " + std::to_string(i + 1) + "/" + std::to_string(edit.rows.size()) + ": " +
                        utf8(input.filename()));
                auto p = run_process(ffmpeg,
                                     {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i", utf8(input),
                                      "-map_metadata", "-1", "-vn", "-ac", "2", "-ar", "44100", "-c:a", "wmav2", "-b:a",
                                      "128k", utf8(audio)},
                                     ctx);
                require(p.code == 0, "FFmpeg failed (exit " + std::to_string(p.code) + "):\n" + p.output);
                input_guard.stable();
                std::string error;
                require(XemuCsb::ValidateWma2XboxProfile(utf8(audio), error), error);
                require(XemuCsb::ReadAsfDurationMs(utf8(audio), track.duration_ms, error), error);
                require(track.duration_ms > 0, "Converted WMA has no positive duration.");
                const auto filename = XemuCsb::XboxSoundtrackFileName(edit.id, track.song_id);
                require(!child(next_folder, filename), "New WMA name collides with an existing unlisted file.");
                next_folder.children.push_back(staged_file(audio, filename, ctx));
            }
            next.tracks.push_back(std::move(track));
        }
        if (rec != records.end())
            *rec = std::move(next);
        else
            records.push_back(std::move(next));
    }
    const auto db_path = work / "database.new";
    std::string error;
    XemuCsb::StDbWriteOptions floors{catalog.metadata.next_soundtrack_id, catalog.metadata.next_song_id};
    require(XemuCsb::WriteStDb(utf8(db_path), records, error, floors), error);
    std::vector<StDbSoundtrack> decoded;
    StDbMetadata metadata;
    require(XemuCsb::ReadStDbFile(utf8(db_path), decoded, error, &metadata), error);
    same_records(records, decoded);
    require(metadata.soundtrack_id_index_matches_records && metadata.next_song_id_covers_records &&
                metadata.next_soundtrack_id_covers_records,
            "Rebuilt ST.DB index/counters failed verification.");
    auto& tdata = directory(vol.root, "TDATA");
    auto& title = directory(tdata, "fffe0000");
    auto& music = directory(title, "music");
    music.children.erase(std::remove_if(music.children.begin(), music.children.end(),
                                        [&](const Node& n) { return ascii_lower(n.name) == ascii_lower(folder); }),
                         music.children.end());
    if (!edit.delete_soundtrack)
        music.children.push_back(std::move(next_folder));
    Node db = staged_file(db_path, db_name, ctx);
    // old_db is no longer used after changing the music vector.
    auto found_db = std::find_if(music.children.begin(), music.children.end(),
                                 [](const Node& n) { return ascii_lower(n.name) == "st.db"; });
    if (found_db != music.children.end()) {
        db.name = found_db->name;
        db.attributes = found_db->attributes;
        db.times = found_db->times;
        music.children.erase(found_db);
    }
    music.children.push_back(std::move(db));
    ctx.say(
        "ST.DB index, global counters, titles and durations verified. Rebuilding a new clean output; source remains unchanged.");
}
} // namespace

Catalog catalog_from_snapshot(const std::vector<Volume>& vols, const Read& read, const Bytes& system,
                              const fs::path& source, Context& ctx) {
    Catalog c;
    c.source = source;
    c.fingerprint = fingerprint(vols, system);
    const auto& vol = e_volume(vols);
    const auto* music = music_node(vol);
    if (!music)
        return c;
    const auto* db = child(*music, "ST.DB");
    if (db) {
        c.has_database = true;
        std::string error;
        std::vector<StDbSoundtrack> records;
        if (!XemuCsb::ReadStDb(read_node(read, vol, *db, ctx), records, error, &c.metadata)) {
            warn(c, "ST.DB cannot be edited: " + error, true);
        } else {
            for (const auto& record : records) {
                Soundtrack s;
                s.record = record;
                auto* folder = child(*music, XemuCsb::XboxSoundtrackFolderName(record.soundtrack_id));
                s.missing = !folder || !folder->directory();
                if (folder && folder->directory())
                    s.bytes = folder_bytes(*folder);
                std::set<uint16_t> seen;
                for (const auto& track : record.tracks) {
                    c.reserved_serials.insert(track.song_id);
                    if (!seen.insert(track.song_id).second)
                        warn(c, "Duplicate song ID inside soundtrack " + std::to_string(record.soundtrack_id), true);
                    const auto* f =
                        folder && folder->directory()
                            ? child(*folder, XemuCsb::XboxSoundtrackFileName(record.soundtrack_id, track.song_id))
                            : nullptr;
                    if (!f || f->directory())
                        s.missing = true;
                }
                if (s.missing)
                    warn(c,
                         "Missing WMA/folder for soundtrack " +
                             XemuCsb::XboxSoundtrackFolderName(record.soundtrack_id) +
                             "; source needs separate recovery.",
                         true);
                c.soundtracks.push_back(std::move(s));
            }
            if (!c.metadata.soundtrack_id_index_matches_records || !c.metadata.next_song_id_covers_records ||
                !c.metadata.next_soundtrack_id_covers_records)
                warn(
                    c,
                    "ST.DB index/counters need TEST12 repair. SAVE a listed soundtrack to a new output, even without track edits.");
        }
    } else if (!music->children.empty()) {
        warn(c, "Music files exist without ST.DB. Orphan data is inspect-only, not silently adopted.", true);
    }
    for (const auto& folder : music->children) {
        uint32_t id = 0;
        if (!parse_hex(folder.name, 4, id))
            continue;
        if (!folder.directory()) {
            warn(c, "A non-directory occupies soundtrack ID " + folder.name, true);
            continue;
        }
        for (const auto& f : folder.children) {
            auto name = ascii_lower(f.name);
            uint32_t song = 0;
            if (!f.directory() && name.size() == 12 && name.substr(8) == ".wma" &&
                parse_hex(name.substr(0, 8), 8, song))
                c.reserved_serials.insert(static_cast<uint16_t>(song));
        }
        if (std::any_of(c.soundtracks.begin(), c.soundtracks.end(),
                        [&](const auto& s) { return s.record.soundtrack_id == id; }))
            continue;
        Soundtrack s;
        s.record.soundtrack_id = static_cast<uint16_t>(id);
        s.record.name = "[Unlisted folder - read only]";
        s.orphan = true;
        s.bytes = folder_bytes(folder);
        for (const auto& f : folder.children) {
            auto name = ascii_lower(f.name);
            uint32_t packed = 0;
            if (!f.directory() && name.size() == 12 && name.substr(8) == ".wma" &&
                parse_hex(name.substr(0, 8), 8, packed) && packed >> 16 == id)
                s.record.tracks.push_back({static_cast<uint16_t>(packed), 0, f.name});
        }
        c.soundtracks.push_back(std::move(s));
        warn(c, "Unlisted soundtrack folder " + folder.name + " retained as read-only.");
    }
    return c;
}
Catalog read_catalog(const Options& requested, Context& ctx) {
    Options o = requested;
    o.mode = Mode::Analyze;
    o.source = absolute_safe(o.source, true);
    Catalog catalog;
    o.content_hooks = std::make_shared<ContentHooks>();
    o.content_hooks->inspect = [&](const auto& vols, const auto& read, const auto& system, auto& context) {
        catalog = catalog_from_snapshot(vols, read, system, o.source, context);
    };
    convert(o, ctx);
    return catalog;
}
Saved save(const Catalog& loaded, const Edit& edit, const SaveOptions& requested, Context& ctx) {
    Options o = requested.disk;
    require(o.offline_confirmed, "Close XEMU/disk tools and confirm offline operation before SAVE.");
    o.source = absolute_safe(o.source, true);
    o.destination = absolute_safe(o.destination, false);
    require(!loaded.fingerprint.empty() && fs::equivalent(o.source, loaded.source),
            "Source changed since the catalog was loaded. Refresh before SAVE.");
    validate_edit(loaded, edit);
    const auto parent = absolute_safe(o.destination.parent_path(), true);
    independent_paths(o.source, o.destination, parent);
    auto backup_root = parent / "Backups";
    require(!is_within(backup_root, o.source) && !is_within(backup_root, o.destination) &&
                !is_within(o.destination, backup_root),
            "The dedicated Backups directory must be outside the source and destination trees.");
    if (fs::exists(fs::symlink_status(backup_root))) {
        backup_root = absolute_safe(backup_root, true);
        require(fs::is_directory(backup_root), "Backups is not a directory.");
    } else {
        absolute_safe(backup_root, false);
        require(fs::create_directory(backup_root), "Cannot create Backups directory.");
    }
    independent_paths(o.source, o.destination, backup_root);
    const auto backup = backup_root / ("CSB-" + random_token());
    require(fs::create_directory(backup), "Cannot create unique CSB backup transaction.");
#ifndef _WIN32
    fs::permissions(backup, fs::perms::owner_all, fs::perm_options::replace);
#endif
    try {
        ctx.say("CSB backup/recovery folder: " + utf8(backup));
        write_text_new(
            backup / "transaction.json",
            "{\"source\":" + quote_json(utf8(o.source)) + ",\"destination\":" + quote_json(utf8(o.destination)) +
                ",\"soundtrack_id\":" + std::to_string(edit.id) +
                ",\"source_fingerprint\":" + quote_json(loaded.fingerprint) +
                ",\"scope\":\"Existing database and affected soundtrack only; original complete HDD is retained unchanged.\"}\n");
        o.mode = requested.folder_output ? (fs::is_directory(o.source) ? Mode::RebuildFolder : Mode::ImageToFolder)
                                         : (fs::is_directory(o.source) ? Mode::FolderToImage : Mode::CleanImage);
        o.workspace_parent = backup;
        o.content_hooks = std::make_shared<ContentHooks>();
        o.content_hooks->inspect = [&](const auto& vols, const auto&, const auto& system, auto&) {
            require(fingerprint(vols, system) == loaded.fingerprint,
                    "The disk contents changed since REFRESH. No edits were applied; reload the catalog.");
        };
        o.content_hooks->edit = [&](auto& vols, const auto& read, const auto& stage, auto& context) {
            apply_edit(vols, read, loaded, edit, backup, stage, requested.ffmpeg, context);
        };
        auto result = convert(o, ctx);
        // Publication has succeeded. A receipt failure must not mislabel the installed output.
        try {
            write_text_new(backup / "complete.json",
                           "{\"status\":\"VERIFIED_NEW_OUTPUT\",\"output\":" + quote_json(utf8(result.destination)) +
                               ",\"report\":" + quote_json(utf8(result.report)) + ",\"source_modified\":false}\n");
            sync_directory(backup);
        } catch (const std::exception& e) {
            if (ctx.log)
                ctx.log("Output verified; backup receipt warning: " + std::string(e.what()));
        }
        if (ctx.log)
            ctx.log("Backup retained: " + utf8(backup));
        return {result, backup};
    } catch (const std::exception& e) {
        const std::string message =
            "CSB did not write to the source. Backup/recovery files retained at: " + utf8(backup) + "\n" + e.what();
        try {
            write_text_new(backup / "csb-failure.txt", message + "\n");
        } catch (...) {
        }
        throw Error(message);
    }
}
void validate_name(const std::string& text, size_t maximum) {
    size_t units = 0;
    for (size_t i = 0; i < text.size();) {
        const uint8_t first = static_cast<uint8_t>(text[i]);
        uint32_t cp = 0;
        size_t n = 0;
        if (first < 0x80) {
            cp = first;
            n = 1;
        } else if ((first & 0xe0) == 0xc0) {
            cp = first & 31;
            n = 2;
        } else if ((first & 0xf0) == 0xe0) {
            cp = first & 15;
            n = 3;
        } else if ((first & 0xf8) == 0xf0) {
            cp = first & 7;
            n = 4;
        }
        require(n && i + n <= text.size(), "Name is not valid UTF-8.");
        for (size_t j = 1; j < n; ++j) {
            const auto c = static_cast<uint8_t>(text[i + j]);
            require((c & 0xc0) == 0x80, "Name is not valid UTF-8.");
            cp = (cp << 6) | (c & 63);
        }
        require(!(n == 2 && cp < 0x80) && !(n == 3 && cp < 0x800) && !(n == 4 && cp < 0x10000) && cp <= 0x10ffff &&
                    !(cp >= 0xd800 && cp <= 0xdfff) && cp >= 32 && cp != 127,
                "Name contains invalid text/control characters.");
        units += cp > 0xffff ? 2 : 1;
        i += n;
    }
    require(units <= maximum, "Name exceeds " + std::to_string(maximum) + " UTF-16 units; shorten it before saving.");
}
std::string title_from_path(const fs::path& path) {
    std::string s = utf8(path.stem());
    // Trim whole UTF-8 scalars only; never split a surrogate pair in the database.
    while (!s.empty()) {
        try {
            validate_name(s, 31);
            return s;
        } catch (const Error&) {
            size_t i = s.size() - 1;
            while (i && (static_cast<uint8_t>(s[i]) & 0xc0) == 0x80)
                --i;
            s.resize(i);
        }
    }
    return "Track";
}
std::string duration(uint32_t ms) {
    if (!ms)
        return "--:--";
    uint64_t seconds = ms / 1000;
    std::ostringstream out;
    if (seconds >= 3600)
        out << seconds / 3600 << ':' << std::setw(2) << std::setfill('0') << (seconds / 60) % 60;
    else
        out << seconds / 60;
    out << ':' << std::setw(2) << std::setfill('0') << seconds % 60;
    return out.str();
}
fs::path default_ffmpeg_path() {
#ifdef _WIN32
    return executable_directory() / "tools" / "ffmpeg.exe";
#else
    return executable_directory() / "tools" / "ffmpeg";
#endif
}
fs::path find_ffmpeg(const fs::path& requested) {
    if (!requested.empty())
        return absolute_safe(requested, true);
#ifdef _WIN32
    const char* name = "ffmpeg.exe";
    const char sep = ';';
#else
    const char* name = "ffmpeg";
    const char sep = ':';
#endif
    std::vector<fs::path> paths{default_ffmpeg_path(), executable_directory() / name};
    if (const char* env = std::getenv("PATH")) {
        std::string text(env);
        size_t start = 0;
        for (;;) {
            size_t end = text.find(sep, start);
            auto p = text.substr(start, end == std::string::npos ? end : end - start);
            if (!p.empty())
                paths.push_back(fs::u8path(p) / name);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return absolute_safe(p, true);
    }
    throw Error(
        "FFmpeg is needed only for new local audio. Put a trusted ffmpeg.exe in tools/ or select it in Helpers. Existing HDD tracks are never re-encoded.");
}
void Editor::set_catalog(Catalog catalog) {
    catalog_ = std::move(catalog);
    edit_ = {};
    selected_ = read_only_ = dirty_ = false;
    ++generation_;
}
void Editor::select(uint16_t id) {
    auto it = std::find_if(catalog_.soundtracks.begin(), catalog_.soundtracks.end(),
                           [&](const auto& s) { return s.record.soundtrack_id == id; });
    require(it != catalog_.soundtracks.end(), "Soundtrack is not in the loaded catalog.");
    edit_ = {};
    edit_.id = id;
    edit_.name = it->record.name;
    edit_.existing = true;
    for (const auto& t : it->record.tracks)
        edit_.rows.push_back({t, {}});
    selected_ = true;
    read_only_ = it->orphan;
    dirty_ = false;
    ++generation_;
}
void Editor::new_soundtrack() {
    require(catalog_.writable && !catalog_.fingerprint.empty(), "Load a writable source first.");
    require(std::count_if(catalog_.soundtracks.begin(), catalog_.soundtracks.end(),
                          [](const auto& s) { return !s.orphan; }) < 100,
            "The database already has 100 soundtracks.");
    uint32_t id = catalog_.metadata.next_soundtrack_id;
    while (id <= 0xffff && std::any_of(catalog_.soundtracks.begin(), catalog_.soundtracks.end(),
                                       [&](const auto& s) { return s.record.soundtrack_id == id; }))
        ++id;
    require(id <= 0xffff, "Soundtrack ID space exhausted.");
    edit_ = {};
    edit_.id = static_cast<uint16_t>(id);
    edit_.name = "XEMU Custom Soundtrack";
    selected_ = true;
    read_only_ = false;
    dirty_ = true;
    ++generation_;
}
bool Editor::can_save() const {
    return selected_ && !read_only() && !edit_.name.empty() && !edit_.rows.empty() &&
           (dirty_ || !catalog_.metadata.soundtrack_id_index_matches_records ||
            !catalog_.metadata.next_song_id_covers_records || !catalog_.metadata.next_soundtrack_id_covers_records);
}
void Editor::rename_soundtrack(const std::string& name) {
    require(selected_ && !read_only(), "Select an editable soundtrack.");
    validate_name(name, 63);
    if (name == edit_.name)
        return;
    edit_.name = name;
    dirty_ = true;
    ++generation_;
}
void Editor::rename_track(size_t index, const std::string& title) {
    require(selected_ && !read_only() && index < edit_.rows.size(), "Select an editable track.");
    validate_name(title, 31);
    if (title == edit_.rows[index].track.title)
        return;
    edit_.rows[index].track.title = title;
    dirty_ = true;
    ++generation_;
}
void Editor::add_files(const std::vector<fs::path>& paths) {
    require(selected_ && !read_only(), "Select/create a soundtrack before adding audio.");
    require(edit_.rows.size() + paths.size() <= 504, "A soundtrack supports at most 504 songs.");
    auto pending = edit_;
    for (const auto& path : paths) {
        auto p = absolute_safe(path, true);
        require(fs::is_regular_file(p), "Drop audio files, not folders.");
        const auto ext = ascii_lower(utf8(p.extension()));
        require(ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".wma" || ext == ".m4a" || ext == ".aac" ||
                    ext == ".ogg" || ext == ".opus" || ext == ".aif" || ext == ".aiff",
                "Unsupported audio extension: " + ext);
        Row r;
        r.local_file = p;
        r.track.song_id = static_cast<uint16_t>(next_serial(catalog_, pending));
        r.track.title = title_from_path(p);
        pending.rows.push_back(std::move(r));
    }
    if (!paths.empty()) {
        edit_ = std::move(pending);
        dirty_ = true;
        ++generation_;
    }
}
void Editor::remove_track(size_t index) {
    require(selected_ && !read_only() && index < edit_.rows.size(), "Select an editable track.");
    edit_.rows.erase(edit_.rows.begin() + static_cast<std::ptrdiff_t>(index));
    dirty_ = true;
    ++generation_;
}
void Editor::clear() {
    require(selected_ && !read_only(), "Select an editable soundtrack.");
    edit_.rows.clear();
    dirty_ = true;
    ++generation_;
}
bool Editor::reorder(size_t from, size_t boundary, uint64_t generation) {
    if (!selected_ || read_only() || generation != generation_ || from >= edit_.rows.size() ||
        boundary > edit_.rows.size())
        return false;
    if (boundary == from || boundary == from + 1)
        return false;
    auto row = std::move(edit_.rows[from]);
    edit_.rows.erase(edit_.rows.begin() + static_cast<std::ptrdiff_t>(from));
    if (boundary > from)
        --boundary;
    edit_.rows.insert(edit_.rows.begin() + static_cast<std::ptrdiff_t>(boundary), std::move(row));
    dirty_ = true;
    ++generation_;
    return true;
}
} // namespace xhc::csb
