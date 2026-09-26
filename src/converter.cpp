// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/converter.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace xhc {
namespace {
struct Disk {
    std::vector<std::shared_ptr<File>> files;
    Read read;
    std::vector<Volume> volumes;
    Bytes system;
    std::string kind;
    uint64_t original_size = 0;
    uint64_t snapshots = 0;
    fs::path folder;
    std::function<void()> check_extra;
    void stable() {
        for (auto& f : files)
            f->stable();
        if (check_extra)
            check_extra();
    }
};
struct HostItem {
    bool directory = false;
    uint64_t size = 0;
    int64_t mtime = 0;
    std::string digest;
};
using HostMap = std::map<std::string, HostItem>;
uint64_t u64_text(const std::string& s) {
    uint64_t n = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    require(!s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size(),
            "Invalid synchronization-manifest integer.");
    return n;
}
int64_t i64_text(const std::string& s) {
    int64_t n = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    require(!s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size(),
            "Invalid synchronization-manifest timestamp.");
    return n;
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        auto i = s.find(sep, start);
        out.push_back(s.substr(start, i == std::string::npos ? i : i - start));
        if (i == std::string::npos)
            break;
        start = i + 1;
    }
    return out;
}
HostMap load_baseline(const fs::path& root) {
    auto text = read_text(root / "Images" / "ce-sync-baseline.tsv", 64 * 1024 * 1024);
    auto lines = split(text, '\n');
    require(!lines.empty() && lines[0] == "CMP-CE-IMAGE-MANIFEST-1", "Unknown Folder-HDD synchronization baseline.");
    HostMap m;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            require(i + 1 == lines.size(), "Unexpected blank baseline entry.");
            continue;
        }
        auto fields = split(lines[i], '\t');
        require(fields.size() >= 2, "Malformed baseline entry.");
        bool dir = fields[0] == "D";
        require(dir || fields[0] == "F", "Unknown baseline entry type.");
        require(fields.size() == (dir ? 2u : 4u), "Malformed baseline field count.");
        std::string path = unb64(fields[1]);
        auto parts = split(path, '/');
        require(!parts.empty() && (parts[0] == "C" || parts[0] == "E"), "Unsafe baseline path.");
        for (const auto& part : parts)
            validate_host_component(part);
        HostItem item;
        item.directory = dir;
        if (!dir) {
            item.size = u64_text(fields[2]);
            item.mtime = i64_text(fields[3]);
        }
        require(m.emplace(path, item).second, "Duplicate baseline path.");
        require(m.size() <= 2 * kMaxEntries + 2, "Baseline exceeds entry bound.");
    }
    return m;
}
void walk_host(const fs::path& root, const std::string& relative, HostMap& m, Context& ctx, unsigned depth) {
    ctx.check();
    require(depth <= kMaxDepth, "Host mirror nesting exceeds limit.");
    auto p = root / fs::u8path(relative);
    reject_links(p);
    HostItem entry;
    entry.directory = fs::is_directory(p);
    require(entry.directory || fs::is_regular_file(p), "Nonregular host mirror object: " + utf8(p));
    if (!entry.directory) {
        File f(p);
        entry.size = f.size();
        entry.mtime = unix_mtime(p);
        entry.digest = hash_file(f, ctx);
        f.stable();
    }
    require(m.emplace(relative, entry).second, "Duplicate host mirror path.");
    require(m.size() <= 2 * kMaxEntries + 2, "Host mirror exceeds entry bound.");
    if (entry.directory) {
        std::vector<fs::path> paths;
        std::set<std::string> folded;
        for (const auto& e : fs::directory_iterator(p)) {
            std::string name = utf8(e.path().filename());
            validate_host_component(name);
            require(folded.insert(ascii_lower(name)).second, "Case-colliding names in host mirror.");
            paths.push_back(e.path());
        }
        std::sort(paths.begin(), paths.end());
        for (const auto& child : paths)
            walk_host(root, relative + "/" + utf8(child.filename()), m, ctx, depth + 1);
    }
}
HostMap scan_host(const fs::path& root, Context& ctx) {
    HostMap m;
    walk_host(root, "C", m, ctx, 0);
    walk_host(root, "E", m, ctx, 0);
    return m;
}
void same_baseline(const HostMap& base, const HostMap& host) {
    require(
        base.size() == host.size(),
        "Folder-HDD mirror has pending additions/deletions. Synchronize safely in XEMU, then close XEMU before conversion.");
    for (const auto& x : base) {
        auto it = host.find(x.first);
        require(it != host.end() && it->second.directory == x.second.directory &&
                    (x.second.directory || (it->second.size == x.second.size && it->second.mtime == x.second.mtime)),
                "Folder-HDD mirror differs from its baseline at " + x.first + ". No side was chosen or overwritten.");
    }
}
void walk_nodes(const Node& n, const std::string& path,
                const std::function<void(const Node&, const std::string&)>& fn) {
    fn(n, path);
    for (const auto& c : n.children)
        walk_nodes(c, path + "/" + c.name, fn);
}
void mirror_matches(const std::vector<Volume>& vols, const HostMap& host) {
    size_t count = 0;
    for (const auto& vol : vols) {
        if (vol.layout.letter != 'C' && vol.layout.letter != 'E')
            continue;
        walk_nodes(vol.root, vol.root.name, [&](const Node& n, const std::string& path) {
            ++count;
            auto found = host.find(path);
            require(found != host.end() && found->second.directory == n.directory() &&
                        (n.directory() || (found->second.size == n.length && found->second.digest == n.digest)),
                    "Folder-HDD image/mirror content conflict at " + path + ". No side was chosen or overwritten.");
        });
    }
    require(count == host.size(), "Extra files exist in the Folder-HDD mirror.");
}
void save_baseline(const fs::path& root, Context& ctx) {
    auto host = scan_host(root, ctx);
    std::ostringstream s;
    s << "CMP-CE-IMAGE-MANIFEST-1\n";
    for (const auto& x : host) {
        s << (x.second.directory ? "D" : "F") << "\t" << b64(x.first);
        if (!x.second.directory)
            s << "\t" << x.second.size << "\t" << x.second.mtime;
        s << "\n";
    }
    write_text_new(root / "Images" / "ce-sync-baseline.tsv", s.str());
}
void scan(Disk& d, Context& ctx) {
    d.volumes.clear();
    for (const auto& p : kLayout) {
        ctx.say("Reading and hashing live files on " + std::string(1, p.letter) + ": ...");
        auto v = scan_volume(d.read, p, ctx);
        hash_volume(d.read, v, ctx);
        ctx.say("  " + std::string(1, p.letter) + ": valid; " + std::to_string(v.free_clusters) + " free clusters; " +
                std::to_string(v.deleted_entries) + " deleted directory entries ignored.");
        d.volumes.push_back(std::move(v));
    }
}
std::string qemu(const fs::path& exe, const std::vector<std::string>& args, Context& ctx) {
    auto r = run_process(exe, args, ctx);
    require(r.code == 0, "qemu-img failed (exit " + std::to_string(r.code) + "):\n" + r.output);
    return r.output;
}
std::string json_source(const fs::path& path, const std::string& driver) {
    // Native Windows source handles deny write/delete sharing. QEMU's Windows
    // file driver rejects locking=on; Linux additionally requires QEMU locks.
#ifdef _WIN32
    const char* locking = "auto";
#else
    const char* locking = "on";
#endif
    return "json:{\"driver\":" + quote_json(driver) +
           ",\"file\":{\"driver\":\"file\",\"filename\":" + quote_json(utf8(path)) +
           ",\"locking\":" + quote_json(locking) + "}}";
}
Json image_info(const fs::path& exe, const std::string& src, Context& ctx) {
    return Json::parse(qemu(exe, {"info", "--output=json", src}, ctx));
}
void check_virtual_size(uint64_t size) {
    require(
        size >= kDiskSize && size <= 0x200000000ULL,
        "Unsupported disk size. v0.6 accepts the standard 8,004,132,864-byte layout or an 8-GiB image with a zero unpartitioned tail; expanded F/G layouts are refused.");
}
Disk open_image(const Options& o, const fs::path& stage, Context& ctx) {
    Disk d;
    auto source = std::make_shared<File>(o.source);
    d.files.push_back(source);
    require(source->size() >= 104, "Image is too small.");
    auto h = source->read(0, 104);
    std::shared_ptr<File> raw;
    require(o.expected_source == SourceKind::Auto ||
                (o.expected_source == SourceKind::Qcow2 && be32(h.data()) == 0x514649fbu) ||
                (o.expected_source == SourceKind::Raw && be32(h.data()) != 0x514649fbu),
            "The selected source format does not match the image header. Select QCOW2 or RAW correctly.");
    if (be32(h.data()) == 0x514649fbu) {
        d.kind = "qcow2";
        uint32_t version = be32(h.data() + 4);
        require(version == 2 || version == 3, "Unsupported QCOW version.");
        require(
            be64(h.data() + 8) == 0 && be32(h.data() + 16) == 0,
            "Backing-chain images are not supported in v0.6. Supply a standalone current disk; source was not changed.");
        require(be32(h.data() + 32) == 0, "Encrypted QCOW2 is not supported.");
        if (version == 3)
            require((be64(h.data() + 72) & 7) == 0,
                    "QCOW2 is dirty/corrupt or uses an external data file. No automatic repair is performed.");
        d.snapshots = be32(h.data() + 60);
        require(
            allow_snapshot_exclusion(o, d.snapshots, ctx),
            "The QCOW2 contains internal snapshots/save states. Clean conversion exports only CURRENT files. Acknowledge snapshot exclusion explicitly; the original retains all snapshots.");
        d.original_size = be64(h.data() + 24);
        check_virtual_size(d.original_size);
        auto exe = find_qemu(o.qemu_img);
        auto src = json_source(o.source, "qcow2");
        ctx.say("Checking the QCOW2 container read-only (no repair, no force-share)...");
        auto info = image_info(exe, src, ctx);
        require(info.at("format").text() == "qcow2" && info.at("virtual-size").u64() == d.original_size,
                "QCOW2 header/info disagreement.");
        require(!info.contains("backing-filename") && !info.contains("full-backing-filename"),
                "Backing-chain input refused.");
        qemu(exe, {"check", "--output=json", src}, ctx);
        source->stable();
        auto space = fs::space(stage);
        require(space.available > d.original_size + 256 * 1024 * 1024ULL,
                "Insufficient workspace for a stable decoded source image.");
        auto decoded = stage / "source-snapshot.raw";
        ctx.say("Decoding the current QCOW2 disk into private temporary storage...");
        qemu(exe, {"convert", "-O", "raw", "-S", "4096", src, utf8(decoded)}, ctx);
        source->stable();
        raw = std::make_shared<File>(decoded);
        d.files.push_back(raw);
        require(raw->size() == d.original_size, "Decoded source size differs.");
    } else {
        d.kind = "raw";
        d.original_size = source->size();
        check_virtual_size(d.original_size);
        raw = source;
    }
    d.system = raw->read(0, size_t(kSystemArea));
    require(std::memcmp(d.system.data(), "****PARTINFO****", 16) != 0,
            "XBPartitioner/custom partition-table images are not supported in v0.6; no partitions were discarded.");
    if (raw->size() > kDiskSize) {
        ctx.say("Checking the unpartitioned disk tail...");
        try {
            raw->verify_zero(kDiskSize, raw->size() - kDiskSize, ctx);
        } catch (const Cancelled&) {
            throw;
        } catch (const Error&) {
            throw Error(
                "Nonzero data exists beyond the supported C/E/X/Y/Z layout. Expanded/unknown disk contents are not silently removed.");
        }
    }
    d.read = [raw](uint64_t off, void* p, size_t n) { raw->read(off, p, n); };
    scan(d, ctx);
    return d;
}
Disk open_folder(const Options& o, Context& ctx, bool check_sync = true) {
    Disk d;
    d.kind = "folder-hdd";
    d.folder = o.source;
    d.original_size = kDiskSize;
    require(
        !fs::exists(fs::symlink_status(o.source / "Images" / "ce-image-dirty.flag")),
        "Folder-HDD has pending/uncertain Xbox writes. Resolve synchronization in XEMU and close it before conversion.");
    auto cpath = absolute_safe(o.source / "Images" / "C.img", true),
         epath = absolute_safe(o.source / "Images" / "E.img", true),
         kpath = absolute_safe(o.source / "Cache" / "xbox_cache.img", true);
    auto c = std::make_shared<File>(cpath), e = std::make_shared<File>(epath), k = std::make_shared<File>(kpath);
    d.files = {c, e, k};
    require(c->size() == kLayout[0].size && e->size() == kLayout[1].size && k->size() == kCacheEnd - kSystemArea,
            "Folder-HDD images do not match the supported TEST12 sizes.");
    d.system.assign(size_t(kSystemArea), 0);
    auto meta = o.source / "Converter-Metadata" / "system-area.bin";
    if (fs::exists(fs::symlink_status(meta))) {
        absolute_safe(meta, true);
        auto f = std::make_shared<File>(meta);
        require(f->size() == kSystemArea, "Invalid preserved system-area sidecar length.");
        d.system = f->read(0, size_t(kSystemArea));
        d.files.push_back(f);
        require(std::memcmp(d.system.data(), "****PARTINFO****", 16) != 0,
                "Unsupported custom partition table in system-area sidecar.");
    }
    auto system = std::make_shared<Bytes>(d.system);
    d.read = [c, e, k, system](uint64_t off, void* p, size_t n) {
        require(off <= kDiskSize && n <= kDiskSize - off, "Read outside Folder-HDD disk.");
        if (off < kSystemArea) {
            require(n <= kSystemArea - off, "System-area read crosses boundary.");
            std::memcpy(p, system->data() + off, n);
            return;
        }
        if (off < kCacheEnd) {
            require(n <= kCacheEnd - off, "Cache read crosses boundary.");
            k->read(off - kSystemArea, p, n);
            return;
        }
        if (off < kLayout[1].offset) {
            require(n <= kLayout[1].offset - off, "C: read crosses boundary.");
            c->read(off - kLayout[0].offset, p, n);
            return;
        }
        e->read(off - kLayout[1].offset, p, n);
    };
    scan(d, ctx);
    for (size_t i = 0; i < 5; ++i) {
        require(d.volumes[i].cluster_bytes == kOutputCluster, "Folder-HDD cluster size is not TEST12-compatible.");
        if (i < 2)
            require(d.volumes[i].volume_id == (0x434d5000u | uint8_t(kLayout[i].letter)),
                    "Folder-HDD C/E volume identity is not TEST12-compatible.");
        else
            require(d.volumes[i].root.chain[0] == 1, "Folder-HDD cache root is not compatible.");
    }
    if (check_sync) {
        ctx.say("Checking the synchronization baseline AND host/image file hashes...");
        absolute_safe(o.source / "Images" / "ce-sync-baseline.tsv", true);
        auto manifest = std::make_shared<File>(o.source / "Images" / "ce-sync-baseline.tsv");
        d.files.push_back(manifest);
        auto base = load_baseline(o.source);
        auto host = scan_host(o.source, ctx);
        same_baseline(base, host);
        mirror_matches(d.volumes, host);
        auto root = o.source;
        auto vols = d.volumes;
        d.check_extra = [root, base, vols, &ctx]() {
            require(!fs::exists(fs::symlink_status(root / "Images" / "ce-image-dirty.flag")),
                    "Folder-HDD became dirty during conversion.");
            auto current = scan_host(root, ctx);
            same_baseline(base, current);
            mirror_matches(vols, current);
        };
    }
    return d;
}
void create_mirrors(const fs::path& root, Disk& d, Context& ctx) {
    for (const auto& vol : d.volumes) {
        if (vol.layout.letter != 'C' && vol.layout.letter != 'E')
            continue;
        std::function<void(const Node&, const fs::path&)> copy = [&](const Node& n, const fs::path& p) {
            ctx.check();
            validate_host_component(n.name);
            if (n.directory()) {
                require(fs::create_directory(p), "Cannot create mirror directory: " + utf8(p));
                for (const auto& child : n.children)
                    copy(child, p / fs::u8path(child.name));
                sync_directory(p);
            } else {
                File out(p, true, n.length);
                Sha256 h;
                uint64_t pos = 0;
                stream_node(
                    d.read, vol, n,
                    [&](const uint8_t* data, size_t size) {
                        out.write(pos, data, size);
                        h.add(data, size);
                        pos += size;
                    },
                    ctx);
                out.flush();
                require(h.finish() == n.digest, "Mirror payload verification failed.");
            }
        };
        copy(vol.root, root / std::string(1, vol.layout.letter));
    }
    save_baseline(root, ctx);
    auto host = scan_host(root, ctx);
    same_baseline(load_baseline(root), host);
    mirror_matches(d.volumes, host);
}
void validate_mirror_names(const std::vector<Volume>& vols) {
    for (const auto& v : vols) {
        if (v.layout.letter != 'C' && v.layout.letter != 'E')
            continue;
        walk_nodes(v.root, v.root.name, [](const Node& n, const std::string&) { validate_host_component(n.name); });
    }
}
std::string report(const Options& o, const Disk& input, const std::vector<Volume>& rebuilt,
                   const std::string& output_hash, uint64_t allocated, const std::string& state) {
    Sha256 systemhash;
    systemhash.add(input.system.data(), input.system.size());
    std::ostringstream s;
    s << "{\n\"program\":\"Xemu HDD Tools\",\"version\":\"0.6b\",\"status\":" << quote_json(state)
      << ",\n\"source\":" << quote_json(utf8(o.source)) << ",\"source_kind\":" << quote_json(input.kind)
      << ",\"source_virtual_bytes\":" << input.original_size << ",\n\"destination\":" << quote_json(utf8(o.destination))
      << ",\"output_virtual_bytes\":" << kDiskSize << ",\"output_allocated_bytes\":" << allocated
      << ",\"container_sha256\":" << quote_json(output_hash) << ",\n\"live_files\":" << node_count(input.volumes)
      << ",\"live_file_bytes\":" << payload_bytes(input.volumes)
      << ",\"excluded_internal_snapshots\":" << input.snapshots
      << ",\n\"clean_rebuild\":true,\"copied_deleted_entries\":false,\"copied_free_cluster_contents\":false,\"copied_file_slack\":false,\"copied_directory_slack\":false,\n\"source_modified\":false,\"system_area_bytes\":"
      << kSystemArea << ",\"system_area_sha256\":" << quote_json(systemhash.finish())
      << ",\n\"scope\":\"Current live C/E/X/Y/Z files including retained recovery and cache files. Required FATX metadata is rebuilt. The reserved system area is preserved separately. Snapshots, deleted remnants, orphan recovery, expanded F/G, and physical ATA security are not migrated.\",\n\"partitions\":[";
    for (size_t i = 0; i < input.volumes.size(); ++i) {
        if (i)
            s << ",";
        auto& v = input.volumes[i];
        s << "{\"letter\":" << quote_json(std::string(1, v.layout.letter))
          << ",\"ignored_deleted_entries\":" << v.deleted_entries << ",\"source_free_clusters\":" << v.free_clusters
          << ",\"source_cluster_bytes\":" << v.cluster_bytes << ",\"output_free_clusters\":" << rebuilt[i].free_clusters
          << "}";
    }
    s << "],\n\"files\":" << inventory_json(input.volumes) << "\n}\n";
    return s.str();
}
}

bool allow_snapshot_exclusion(const Options& o, uint64_t snapshots, Context& ctx) {
    ctx.check();
    if (o.mode == Mode::Analyze || !snapshots || o.acknowledge_snapshot_exclusion)
        return true;
    if (!o.confirm_snapshot_exclusion)
        return false;
    const bool approved = o.confirm_snapshot_exclusion(snapshots);
    ctx.check();
    return approved;
}

Result convert(const Options& requested, Context& ctx) {
    Options o = requested;
    require(o.offline_confirmed, "Confirm XEMU and all other disk tools are CLOSED. This converter is offline-only.");
    o.source = absolute_safe(o.source, true);
    bool folder = fs::is_directory(o.source);
    require(o.expected_source == SourceKind::Auto || (folder == (o.expected_source == SourceKind::Folder)),
            "The source is not the selected file/folder type. Change the global Source selection.");
    require(o.mode != Mode::ImageToFolder || !folder, "Image -> Folder-HDD needs an image file.");
    require(o.mode != Mode::FolderToImage || folder, "Folder-HDD -> QCOW2 needs a Folder-HDD root.");
    require(o.mode != Mode::CleanImage || !folder, "Clean Image needs an image source.");
    require(o.mode != Mode::RebuildFolder || folder, "Folder rebuild needs a Folder-HDD root.");
    bool analyze = o.mode == Mode::Analyze,
         folder_output = o.mode == Mode::ImageToFolder || o.mode == Mode::RebuildFolder;
    fs::path parent = fs::temp_directory_path();
    if (!analyze) {
        o.destination = absolute_safe(o.destination, false);
        parent = o.destination.parent_path();
        require(!is_within(o.destination, o.source) && !is_within(o.source, o.destination),
                "Source and destination must be independent, non-nested locations.");
        require(!fs::exists(fs::symlink_status(fs::path(o.destination).concat(".conversion.json"))),
                "Destination report already exists.");
    }
    if (!o.workspace_parent.empty())
        parent = o.workspace_parent;
    parent = absolute_safe(parent, true);
    require(fs::is_directory(parent), "Workspace parent is not a directory.");
    if (folder) {
        // Check directory identities too: lexical paths alone miss mount aliases.
        for (fs::path ancestor = parent; !ancestor.empty();) {
            std::error_code error;
            const bool same = fs::equivalent(ancestor, o.source, error);
            require(!error, "Cannot verify source/workspace separation: " + error.message());
            require(!same, "The output/workspace must not be inside the source Folder-HDD, including path aliases.");
            auto next = ancestor.parent_path();
            if (next == ancestor)
                break;
            ancestor = std::move(next);
        }
    }
    fs::path stage = parent / (".xhc-transaction-" + random_token());
    require(fs::create_directory(stage), "Cannot create private conversion workspace.");
#ifndef _WIN32
    // Set private permissions inside the reported transaction boundary below.
#endif
    try {
#ifndef _WIN32
        fs::permissions(stage, fs::perms::owner_all, fs::perm_options::replace);
#endif
        ctx.say("Workspace: " + utf8(stage));
        ctx.say("Source is read-only. New output will contain live files, not deleted sectors.");
        Disk input = folder ? open_folder(o, ctx) : open_image(o, stage, ctx);
        const auto original_volumes = input.volumes;
        if (o.content_hooks && o.content_hooks->inspect)
            o.content_hooks->inspect(input.volumes, input.read, input.system, ctx);
        uint64_t backing_allocation = 0;
        if (folder) {
            for (size_t i = 0; i < 3; ++i)
                backing_allocation += allocated_bytes(input.files[i]->path());
        } else
            backing_allocation = allocated_bytes(o.source);
        ctx.say("Source kind: " + input.kind + "; virtual disk bytes: " + std::to_string(input.original_size) +
                "; host backing allocation: " + std::to_string(backing_allocation) +
                " bytes (Folder-HDD mirrors additional).");
        ctx.say("Internal source snapshots: " + std::to_string(input.snapshots) + "; current-file view only.");
        uint64_t live = payload_bytes(input.volumes);
        ctx.say("Validated " + std::to_string(node_count(input.volumes)) + " live files, " + std::to_string(live) +
                " payload bytes across C/E/X/Y/Z.");
        if (analyze) {
            input.stable();
            Result result;
            result.files = node_count(input.volumes);
            result.live_bytes = live;
            input.read = {};
            input.check_extra = {};
            input.files.clear();
            fs::remove_all(stage);
            if (ctx.log)
                ctx.log("Read-only analysis complete. No source content changed.");
            return result;
        }
        if (o.content_hooks && o.content_hooks->edit) {
            o.content_hooks->edit(input.volumes, input.read, stage, ctx);
            live = payload_bytes(input.volumes);
        }
        uint64_t required =
            (folder_output ? kDiskSize + live : (o.raw_output ? kDiskSize : 2 * kDiskSize)) + (512ULL * 1024 * 1024);
        require(
            fs::space(stage).available >= required,
            "Insufficient free workspace. Allow at least " + std::to_string(required) +
                " additional bytes for verified create-new conversion. Actual sparse output often uses less space.");
        if (folder_output)
            validate_mirror_names(input.volumes);
        fs::path ready = stage / (folder_output ? "result" : (o.raw_output ? "result.raw" : "result.qcow2"));
        std::vector<Volume> verified;
        std::string output_hash;
        uint64_t allocation = 0;
        fs::path report_path;
        if (folder_output) {
            fs::create_directory(ready);
            fs::create_directory(ready / "Images");
            fs::create_directory(ready / "Cache");
            fs::create_directory(ready / "Converter-Metadata");
            {
                File c(ready / "Images" / "C.img", true, kLayout[0].size),
                    e(ready / "Images" / "E.img", true, kLayout[1].size),
                    cache(ready / "Cache" / "xbox_cache.img", true, kCacheEnd - kSystemArea);
                for (size_t i = 0; i < 5; ++i) {
                    ctx.say("Rebuilding clean " + std::string(1, kLayout[i].letter) + ": ...");
                    File& out = i == 0 ? c : i == 1 ? e : cache;
                    uint64_t off = i < 2 ? 0 : kLayout[i].offset - kSystemArea;
                    write_volume(out, off, input.read, input.volumes[i], true, ctx);
                }
                c.flush();
                e.flush();
                cache.flush();
            }
            {
                File f(ready / "Converter-Metadata" / "system-area.bin", true, kSystemArea);
                f.write_sparse(0, input.system.data(), input.system.size());
                f.flush();
            }
            {
                Options output_options = o;
                output_options.source = ready;
                Disk output = open_folder(output_options, ctx, false);
                compare_volumes(input.volumes, output.volumes);
                verified = output.volumes;
                ctx.say("Verifying every free region, file tail, directory padding, and live file...");
                for (size_t i = 0; i < 5; ++i) {
                    Volume v = output.volumes[i];
                    v.layout.offset = i < 2 ? 0 : kLayout[i].offset - kSystemArea;
                    verify_clean_volume(*output.files[i < 2 ? i : 2], v, ctx);
                }
                ctx.say("Creating and verifying host C/E mirrors and synchronization baseline...");
                create_mirrors(ready, output, ctx);
                output.stable();
            }
            allocation = allocated_bytes(ready / "Images" / "C.img") + allocated_bytes(ready / "Images" / "E.img") +
                         allocated_bytes(ready / "Cache" / "xbox_cache.img");
            report_path = ready / "Converter-Metadata" / "conversion-report.json";
        } else {
            fs::path raw = stage / "clean.raw";
            {
                File out(raw, true, kDiskSize);
                out.write_sparse(0, input.system.data(), input.system.size());
                for (size_t i = 0; i < 5; ++i) {
                    ctx.say("Rebuilding clean " + std::string(1, kLayout[i].letter) + ": ...");
                    write_volume(out, kLayout[i].offset, input.read, input.volumes[i], false, ctx);
                }
                out.flush();
            }
            {
                File out(raw);
                Read read = [&](uint64_t p, void* b, size_t n) { out.read(p, b, n); };
                for (const auto& l : kLayout) {
                    auto v = scan_volume(read, l, ctx);
                    hash_volume(read, v, ctx);
                    verify_clean_volume(out, v, ctx);
                    verified.push_back(std::move(v));
                }
                compare_volumes(input.volumes, verified);
            }
            if (o.raw_output)
                move_new(raw, ready);
            else {
                auto exe = find_qemu(o.qemu_img);
                ctx.say("Encoding a new standalone sparse QCOW2 (not an overlay)...");
                qemu(exe,
                     {"convert", "-f", "raw", "-O", "qcow2", "-o", "compat=1.1,cluster_size=65536,lazy_refcounts=off",
                      "-S", "4096", utf8(raw), utf8(ready)},
                     ctx);
                auto src = json_source(ready, "qcow2");
                auto info = image_info(exe, src, ctx);
                require(info.at("virtual-size").u64() == kDiskSize && !info.contains("backing-filename") &&
                            !info.contains("full-backing-filename"),
                        "Output is not standalone or has the wrong virtual size.");
                if (info.contains("snapshots"))
                    require(info.at("snapshots").kind == Json::Kind::Array && info.at("snapshots").array.empty(),
                            "Unexpected snapshots in clean output.");
                ctx.say("Checking QCOW2 and comparing all guest sectors against the verified clean disk...");
                qemu(exe, {"check", "--output=json", src}, ctx);
                qemu(exe, {"compare", "-f", "raw", "-F", "qcow2", utf8(raw), utf8(ready)}, ctx);
            }
            {
                File f(ready);
                output_hash = hash_file(f, ctx);
                f.stable();
            }
            allocation = allocated_bytes(ready);
            report_path = stage / "conversion-report.json";
        }
        ctx.say("Rechecking the original source before publication...");
        input.stable();
        auto expected = input.volumes;
        scan(input, ctx);
        compare_volumes(original_volumes, input.volumes);
        input.stable();
        ctx.check();
        input.volumes = std::move(expected);
        write_text_new(report_path, report(o, input, verified, output_hash, allocation, "VERIFIED"));
        if (folder_output) {
            sync_directory(ready / "Images");
            sync_directory(ready / "Cache");
            sync_directory(ready / "Converter-Metadata");
            sync_directory(ready);
            move_new(ready, o.destination);
            report_path = o.destination / "Converter-Metadata" / "conversion-report.json";
        } else {
            auto final_report = fs::path(o.destination).concat(".conversion.json");
            move_new(report_path, final_report);
            report_path = final_report;
            move_new(ready, o.destination);
        }
        Result result{o.destination, report_path, node_count(input.volumes), live};
        input.read = {};
        input.check_extra = {};
        input.files.clear();
        std::error_code cleanup;
        fs::remove_all(stage, cleanup);
        if (cleanup) {
            if (ctx.log)
                ctx.log("Verified output published; temporary workspace retained: " + utf8(stage) + " (" +
                        cleanup.message() + ")");
        }
        if (ctx.log) {
            ctx.log("SUCCESS: " + utf8(o.destination));
            ctx.log("Verification report: " + utf8(report_path));
            ctx.log(
                "Original source retained. Output is a clean CURRENT-file copy; it is not a save-state or deleted-file recovery backup.");
        }
        return result;
    } catch (const std::exception& e) {
        const std::string message =
            "Converter performed no source writes.\nRecovery workspace retained at: " + utf8(stage) +
            "\nReason: " + e.what();
        try {
            write_text_new(stage / "failure.txt", message + "\n");
        } catch (...) {
        }
        throw Error(message);
    }
}
}
