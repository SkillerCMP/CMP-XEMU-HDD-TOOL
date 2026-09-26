// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/hdd.hpp"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace xhc::hdd {
namespace {
std::string key(const std::string& s) { return ascii_lower(s); }
const Node* find(const Node& n, const std::string& name) {
    auto k = key(name);
    for (const auto& c : n.children)
        if (key(c.name) == k)
            return &c;
    return nullptr;
}
Node& node(std::vector<Volume>& v, const Path& p) { return const_cast<Node&>(lookup(v, p)); }
void need_loaded(const Editor& e) { require(e.loaded(), "REFRESH HDD before editing."); }
bool beneath(const Path& p, const Path& parent) {
    if (p.partition != parent.partition || p.components.size() < parent.components.size())
        return false;
    for (size_t i = 0; i < parent.components.size(); ++i)
        if (key(p.components[i]) != key(parent.components[i]))
            return false;
    return true;
}
void names(const Node& root, unsigned depth, size_t& count) {
    require(depth <= kMaxDepth, "Pending tree exceeds the supported nesting depth.");
    require(++count <= kMaxEntries, "Pending tree exceeds the supported entry count.");
    std::set<std::string> seen;
    for (const auto& c : root.children) {
        validate_host_component(c.name);
        require(seen.insert(key(c.name)).second, "Case-colliding pending names.");
        require(c.directory() || c.children.empty(), "A pending file cannot contain children.");
        names(c, depth + 1, count);
    }
}
std::array<uint16_t, 6> now_times() {
    std::time_t time = std::time(nullptr);
    std::tm t{};
#ifdef _WIN32
    gmtime_s(&t, &time);
#else
    gmtime_r(&time, &t);
#endif
    uint16_t clock = uint16_t((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
    uint16_t date =
        uint16_t(((std::clamp(t.tm_year + 1900, 1980, 2107) - 1980) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    return {{clock, date, clock, date, clock, date}};
}
void erase(std::vector<Volume>& vols, const Path& path) {
    require(!path.root(), "Partition roots cannot be removed or renamed.");
    auto& children = node(vols, path.parent()).children;
    auto name = key(path.components.back());
    auto it = std::find_if(children.begin(), children.end(), [&](const Node& n) { return key(n.name) == name; });
    require(it != children.end(), "The selected item no longer exists.");
    children.erase(it);
}
Node host_node(const fs::path& request, const fs::path& source, Context& ctx, unsigned depth, size_t& count) {
    ctx.check();
    require(depth <= kMaxDepth && ++count <= kMaxEntries, "Import exceeds the tree/depth limit.");
    auto path = absolute_safe(request, true);
    require(!is_within(path, source), "Do not import files from inside the source HDD or Folder-HDD.");
    if (fs::is_regular_file(path) && fs::is_regular_file(source))
        require(!fs::equivalent(path, source), "Cannot import the source HDD into itself.");
    Node n;
    n.name = utf8(path.filename());
    validate_host_component(n.name);
    n.times = now_times();
    if (fs::is_directory(path)) {
        n.attributes = 0x10;
        std::set<std::string> seen;
        std::vector<fs::path> children;
        for (const auto& c : fs::directory_iterator(path)) {
            ctx.check();
            require(seen.insert(key(utf8(c.path().filename()))).second, "Case-colliding host names.");
            children.push_back(c.path());
        }
        std::sort(children.begin(), children.end());
        for (const auto& c : children)
            n.children.push_back(host_node(c, source, ctx, depth + 1, count));
        std::set<std::string> after;
        for (const auto& c : fs::directory_iterator(path))
            after.insert(key(utf8(c.path().filename())));
        require(after == seen, "Host folder changed during import planning. Retry.");
    } else {
        require(fs::is_regular_file(path), "Only regular files/directories can be imported.");
        File f(path);
        require(f.size() <= std::numeric_limits<uint32_t>::max(), "File exceeds the FATX 32-bit file-length limit.");
        n.attributes = 0x20;
        n.length = static_cast<uint32_t>(f.size());
        n.digest = hash_file(f, ctx);
        f.stable();
        n.replacement = path;
    }
    return n;
}
void separate(const fs::path& source, const fs::path& destination, const fs::path& parent) {
    require(!is_within(destination, source) && !is_within(source, destination),
            "Source and output must be independent.");
    if (!fs::is_directory(source))
        return;
    for (auto p = parent; !p.empty();) {
        std::error_code ec;
        bool same = fs::equivalent(p, source, ec);
        require(!ec && !same, "Output/Backups must not be inside the source Folder-HDD, including aliases.");
        auto next = p.parent_path();
        if (next == p)
            break;
        p = next;
    }
}
void export_node(const Read& read, const Volume& v, const Node& n, const fs::path& out, Context& ctx) {
    ctx.check();
    validate_host_component(n.name);
    if (n.directory()) {
        require(fs::create_directory(out), "Cannot create export directory: " + utf8(out));
        for (const auto& c : n.children)
            export_node(read, v, c, out / fs::u8path(c.name), ctx);
        sync_directory(out);
    } else {
        {
            File f(out, true, n.length);
            uint64_t pos = 0;
            Sha256 sha;
            stream_node(
                read, v, n,
                [&](const uint8_t* b, size_t length) {
                    f.write_sparse(pos, b, length);
                    sha.add(b, length);
                    pos += length;
                },
                ctx);
            require(pos == n.length && sha.finish() == n.digest, "Source changed during backup/export.");
            f.flush();
        }
        File verify(out);
        require(hash_file(verify, ctx) == n.digest, "Fresh backup/export hash verification failed.");
        verify.stable();
    }
}
bool own_equal(const Node& a, const Node& b) {
    return a.name == b.name && a.attributes == b.attributes && a.times == b.times && a.directory() == b.directory() &&
           (a.directory() || (a.length == b.length && a.digest == b.digest));
}
void ensure_parent(const fs::path& root, const Path& path) {
    fs::path p = root / std::string(1, path.partition);
    if (!fs::exists(p))
        require(fs::create_directory(p), "Cannot create backup partition directory.");
    for (size_t i = 0; i + 1 < path.components.size(); ++i) {
        validate_host_component(path.components[i]);
        p /= fs::u8path(path.components[i]);
        if (!fs::exists(p))
            require(fs::create_directory(p), "Cannot create backup path.");
    }
}
void backup_changed(const Read& read, const Volume& old, const Node& n, const Node* current, const Path& path,
                    const fs::path& backup, Context& ctx, size_t& changes) {
    if (!path.root() && (!current || !own_equal(n, *current))) {
        ensure_parent(backup, path);
        fs::path out = backup / std::string(1, path.partition);
        for (const auto& c : path.components)
            out /= fs::u8path(c);
        export_node(read, old, n, out, ctx);
        ++changes;
        return;
    }
    if (n.directory())
        for (const auto& c : n.children)
            backup_changed(read, old, c, current ? find(*current, c.name) : nullptr, path.child(c.name), backup, ctx,
                           changes);
}
void stage_files(Node& n, const fs::path& stage, Context& ctx, size_t& id) {
    ctx.check();
    if (!n.replacement.empty()) {
        const auto source = absolute_safe(n.replacement, true);
        File in(source);
        require(in.size() == n.length, "Imported file size changed since it was selected: " + utf8(source));
        auto out = stage / ("hdd-payload-" + std::to_string(id++));
        {
            File dest(out, true, n.length);
            Bytes b(kIOChunk);
            Sha256 sha;
            for (uint64_t pos = 0; pos < in.size();) {
                ctx.check();
                auto length = static_cast<size_t>(std::min<uint64_t>(b.size(), in.size() - pos));
                in.read(pos, b.data(), length);
                sha.add(b.data(), length);
                dest.write_sparse(pos, b.data(), length);
                pos += length;
            }
            require(sha.finish() == n.digest, "Imported file changed since selection: " + utf8(source));
            dest.flush();
            in.stable();
        }
        File check(out);
        require(hash_file(check, ctx) == n.digest, "Staged imported file hash mismatch.");
        n.replacement = out;
    }
    for (auto& c : n.children)
        stage_files(c, stage, ctx, id);
}
}
Path Path::parse(const std::string& text) {
    require(text.size() >= 3 && text[1] == ':' && (text[2] == '/' || text[2] == '\\'),
            "Use an absolute FATX path such as E:/UDATA.");
    Path p;
    p.partition = text[0] >= 'a' && text[0] <= 'z' ? static_cast<char>(text[0] - 'a' + 'A') : text[0];
    require(std::string("CEXYZ").find(p.partition) != std::string::npos, "Unsupported partition.");
    size_t start = 3;
    while (start < text.size()) {
        size_t end = text.find_first_of("/\\", start);
        if (end == std::string::npos)
            end = text.size();
        auto name = text.substr(start, end - start);
        validate_host_component(name);
        p.components.push_back(name);
        start = end + 1;
    }
    require(p.components.size() <= kMaxDepth, "FATX path is too deep.");
    return p;
}
std::string Path::str() const {
    std::string s(1, partition);
    s += ":/";
    for (size_t i = 0; i < components.size(); ++i) {
        if (i)
            s += '/';
        s += components[i];
    }
    return s;
}
Path Path::parent() const {
    auto p = *this;
    if (!p.components.empty())
        p.components.pop_back();
    return p;
}
Path Path::child(const std::string& name) const {
    validate_host_component(name);
    auto p = *this;
    p.components.push_back(name);
    require(p.components.size() <= kMaxDepth, "Path nesting limit exceeded.");
    return p;
}
const Volume& volume(const std::vector<Volume>& vols, char letter) {
    for (const auto& v : vols)
        if (v.layout.letter == letter)
            return v;
    throw Error("Partition is not in this snapshot.");
}
const Node& lookup(const std::vector<Volume>& vols, const Path& p) {
    const Node* n = &volume(vols, p.partition).root;
    for (const auto& c : p.components) {
        require(n->directory(), "A file occupies a parent path.");
        n = find(*n, c);
        require(n, "FATX item not found: " + p.str());
    }
    return *n;
}
std::string snapshot_fingerprint(const std::vector<Volume>& v, const Bytes& system) {
    auto inventory = inventory_json(v);
    Sha256 sha;
    sha.add(system.data(), system.size());
    sha.add(inventory.data(), inventory.size());
    return sha.finish();
}
Catalog read_catalog(const Options& request, Context& ctx) {
    Options o = request;
    o.mode = Mode::Analyze;
    o.destination.clear();
    o.workspace_parent.clear();
    o.content_hooks = std::make_shared<ContentHooks>();
    Catalog result;
    o.content_hooks->inspect = [&](const auto& vols, const auto&, const auto& system, auto&) {
        result = {absolute_safe(o.source, true), snapshot_fingerprint(vols, system), vols};
    };
    convert(o, ctx);
    require(!result.fingerprint.empty(), "HDD snapshot was not loaded.");
    return result;
}
void Editor::load(Catalog c) {
    original_ = std::move(c);
    working_ = original_.volumes;
    dirty_ = false;
    ++generation_;
}
void Editor::discard() {
    need_loaded(*this);
    working_ = original_.volumes;
    dirty_ = false;
    ++generation_;
}
void Editor::commit(std::vector<Volume> work) {
    size_t count = 0;
    for (const auto& v : work)
        names(v.root, 0, count);
    working_ = std::move(work);
    dirty_ = true;
    ++generation_;
}
void Editor::new_folder(const Path& parent, const std::string& name) {
    need_loaded(*this);
    validate_host_component(name);
    auto work = working_;
    auto& n = node(work, parent);
    require(n.directory() && !find(n, name), "Destination already exists or is not a directory.");
    Node c;
    c.name = name;
    c.attributes = 0x10;
    c.times = now_times();
    n.children.push_back(std::move(c));
    commit(std::move(work));
}
void Editor::rename(const Path& path, const std::string& name) {
    need_loaded(*this);
    validate_host_component(name);
    require(!path.root(), "Cannot rename a partition root.");
    auto work = working_;
    auto& n = node(work, path);
    if (n.name == name)
        return;
    auto* other = find(node(work, path.parent()), name);
    require(!other || other == &n, "Rename destination already exists.");
    n.name = name;
    commit(std::move(work));
}
void Editor::remove(const Path& path) {
    need_loaded(*this);
    auto work = working_;
    erase(work, path);
    commit(std::move(work));
}
void Editor::copy_move(const Path& source, const Path& target, bool move) {
    need_loaded(*this);
    require(!source.root(), "Select an item, not a partition root.");
    require(source.partition == target.partition,
            "Copy/move currently stays within one partition. Use Export + Import for cross-partition copies.");
    require(!beneath(target, source), "Cannot copy/move a directory into itself.");
    auto work = working_;
    Node copy = lookup(work, source);
    auto& parent = node(work, target);
    require(parent.directory() && !find(parent, copy.name),
            "Destination already exists; no merge/overwrite is performed.");
    if (move)
        erase(work, source);
    node(work, target).children.push_back(std::move(copy));
    commit(std::move(work));
}
void Editor::import_paths(const Path& target, const std::vector<fs::path>& paths, Context& ctx) {
    need_loaded(*this);
    require(!paths.empty(), "No host files selected.");
    auto work = working_;
    auto& parent = node(work, target);
    require(parent.directory(), "Drop onto a directory.");
    size_t count = 0;
    for (const auto& v : work)
        names(v.root, 0, count);
    for (const auto& path : paths) {
        Node n = host_node(path, original_.source, ctx, static_cast<unsigned>(target.components.size() + 1), count);
        require(!find(parent, n.name), "Import destination already exists: " + target.child(n.name).str() +
                                           ". Use Replace File explicitly, or choose a new name.");
        parent.children.push_back(std::move(n));
    }
    commit(std::move(work));
}
void Editor::replace_file(const Path& path, const fs::path& local, Context& ctx) {
    need_loaded(*this);
    require(!path.root(), "Cannot replace a partition.");
    auto work = working_;
    auto& old = node(work, path);
    require(!old.directory(), "Replace File does not replace directories.");
    size_t count = 0;
    Node next = host_node(local, original_.source, ctx, 0, count);
    require(!next.directory(), "Choose a regular replacement file.");
    next.name = old.name;
    next.attributes = old.attributes;
    next.times = old.times;
    old = std::move(next);
    commit(std::move(work));
}
Saved save(const Editor& editor, const SaveOptions& requested, Context& ctx) {
    need_loaded(editor);
    require(editor.dirty(), "There are no pending HDD changes.");
    Options o = requested.disk;
    require(o.offline_confirmed, "Close XEMU and all other source writers before saving.");
    o.source = absolute_safe(o.source, true);
    o.destination = absolute_safe(o.destination, false);
    require(fs::equivalent(o.source, editor.original().source), "Source changed. REFRESH HDD before saving.");
    auto parent = absolute_safe(o.destination.parent_path(), true);
    separate(o.source, o.destination, parent);
    auto root = parent / "Backups";
    require(!is_within(root, o.source) && !is_within(root, o.destination) && !is_within(o.destination, root),
            "Backups must be outside the source/output trees.");
    if (fs::exists(fs::symlink_status(root))) {
        root = absolute_safe(root, true);
        require(fs::is_directory(root), "Backups is not a directory.");
    } else {
        absolute_safe(root, false);
        require(fs::create_directory(root), "Cannot create Backups folder.");
    }
    separate(o.source, o.destination, root);
    const auto backup = root / ("HDD-" + random_token());
    require(fs::create_directory(backup), "Cannot create unique backup transaction.");
#ifndef _WIN32
    fs::permissions(backup, fs::perms::owner_all, fs::perm_options::replace);
#endif
    try {
        ctx.say("HDD Directory backup/recovery folder: " + utf8(backup));
        write_text_new(backup / "transaction.json",
                       "{\"source\":" + quote_json(utf8(o.source)) + ",\"output\":" + quote_json(utf8(o.destination)) +
                           ",\"source_fingerprint\":" + quote_json(editor.original().fingerprint) +
                           ",\"source_modified\":false}\n");
        o.workspace_parent = backup;
        o.mode = requested.folder_output ? (fs::is_directory(o.source) ? Mode::RebuildFolder : Mode::ImageToFolder)
                                         : (fs::is_directory(o.source) ? Mode::FolderToImage : Mode::CleanImage);
        o.content_hooks = std::make_shared<ContentHooks>();
        o.content_hooks->inspect = [&](const auto& vols, const auto&, const auto& system, auto&) {
            require(snapshot_fingerprint(vols, system) == editor.original().fingerprint,
                    "Source changed since REFRESH. Pending edits were not applied.");
        };
        o.content_hooks->edit = [&](auto& vols, const auto& read, const auto& stage, auto& context) {
            context.say("Backing up affected ORIGINAL files to: " + utf8(backup));
            size_t changed = 0;
            for (const auto& v : vols)
                backup_changed(read, v, v.root, &volume(editor.volumes(), v.layout.letter).root,
                               Path{v.layout.letter, {}}, backup, context, changed);
            write_text_new(backup / "original-files.json", inventory_json(vols));
            write_text_new(backup / "backup-verified.txt",
                           "Affected original objects freshly hash-verified: " + std::to_string(changed) +
                               ". Complete original HDD retained.\n");
            auto working = editor.volumes();
            size_t staged = 0;
            for (auto& v : working)
                stage_files(v.root, stage, context, staged);
            size_t count = 0;
            for (const auto& v : working)
                names(v.root, 0, count);
            vols = std::move(working);
            sync_directory(backup);
        };
        auto result = convert(o, ctx);
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
        auto text =
            "HDD Directory did not write to the source. Backup/recovery retained at: " + utf8(backup) + "\n" + e.what();
        try {
            write_text_new(backup / "hdd-failure.txt", text + "\n");
        } catch (...) {
        }
        throw Error(text);
    }
}
fs::path export_items(const Catalog& catalog, const Options& requested, const std::vector<Path>& paths,
                      const fs::path& destination, Context& ctx) {
    require(!catalog.fingerprint.empty() && !paths.empty(), "Load a source and select items to export.");
    Options o = requested;
    o.source = absolute_safe(o.source, true);
    require(fs::equivalent(o.source, catalog.source), "Source changed. Refresh before exporting.");
    auto out = absolute_safe(destination, false);
    auto parent = absolute_safe(out.parent_path(), true);
    separate(o.source, out, parent);
    std::vector<Path> selected;
    std::set<std::string> leaves;
    for (size_t i = 0; i < paths.size(); ++i) {
        bool nested = false;
        for (size_t j = 0; j < paths.size(); ++j)
            if (i != j && beneath(paths[i], paths[j]) && (key(paths[i].str()) != key(paths[j].str()) || j < i)) {
                nested = true;
                break;
            }
        if (!nested) {
            auto& n = lookup(catalog.volumes, paths[i]);
            require(leaves.insert(key(n.name)).second, "Selected exports have colliding top-level names.");
            selected.push_back(paths[i]);
        }
    }
    require(!selected.empty(), "No unique selected export items.");
    auto work = parent / (".xhc-export-" + random_token());
    require(fs::create_directory(work), "Cannot create export workspace.");
    try {
        ctx.say("Export workspace: " + utf8(work));
        auto ready = work / "files";
        require(fs::create_directory(ready), "Cannot stage export.");
        o.mode = Mode::Analyze;
        o.workspace_parent = work;
        o.destination.clear();
        o.content_hooks = std::make_shared<ContentHooks>();
        o.content_hooks->inspect = [&](const auto& vols, const auto& read, const auto& system, auto& context) {
            require(snapshot_fingerprint(vols, system) == catalog.fingerprint,
                    "Source changed since REFRESH. Export refused.");
            for (const auto& p : selected) {
                const auto& n = lookup(vols, p);
                context.say("Exporting " + p.str());
                export_node(read, volume(vols, p.partition), n, ready / fs::u8path(n.name), context);
            }
            write_text_new(work / "source-inventory.json", inventory_json(vols));
        };
        convert(o, ctx);
        ctx.check();
        sync_directory(ready);
        move_new(ready, out);
        // The inventory is auxiliary; failure after publication is a warning, not a false failure.
        try {
            move_new(work / "source-inventory.json", fs::path(out).concat(".hdd-export.json"));
        } catch (const std::exception& e) {
            if (ctx.log)
                ctx.log("Export verified; inventory retained at " + utf8(work) + ": " + e.what());
            return out;
        }
        std::error_code ec;
        fs::remove_all(work, ec);
        if (ec && ctx.log)
            ctx.log("Verified export; temporary work retained: " + utf8(work));
        return out;
    } catch (const std::exception& e) {
        auto text = "Source unchanged. Export recovery workspace retained at: " + utf8(work) + "\n" + e.what();
        try {
            write_text_new(work / "failure.txt", text + "\n");
        } catch (...) {
        }
        throw Error(text);
    }
}
std::string modified_time(const Node& n) {
    auto t = n.times[0], d = n.times[1];
    if (!d)
        return "--";
    std::ostringstream s;
    s << std::setfill('0') << 1980 + (d >> 9) << '-' << std::setw(2) << ((d >> 5) & 15) << '-' << std::setw(2)
      << (d & 31) << ' ' << std::setw(2) << (t >> 11) << ':' << std::setw(2) << ((t >> 5) & 63) << ':' << std::setw(2)
      << ((t & 31) * 2);
    return s.str();
}
std::string attributes(uint8_t n) {
    std::ostringstream s;
    s << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << unsigned(n);
    return s.str();
}
}
