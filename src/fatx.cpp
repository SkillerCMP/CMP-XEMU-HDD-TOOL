// SPDX-License-Identifier: GPL-2.0-or-later
// Clean FATX rebuilds with strict allocation-chain ownership.
#include "xhc/fatx.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>

namespace xhc {
const std::array<Layout, 5> kLayout{{{'C', 0x8CA80000ULL, 0x01F400000ULL},
                                     {'E', 0xABE80000ULL, 0x1312D6000ULL},
                                     {'X', 0x00080000ULL, 0x02EE00000ULL},
                                     {'Y', 0x2EE80000ULL, 0x02EE00000ULL},
                                     {'Z', 0x5DC80000ULL, 0x02EE00000ULL}}};
namespace {
uint64_t align4096(uint64_t x) { return (x + 4095) & ~uint64_t(4095); }
struct Geometry {
    uint32_t bits;
    uint64_t fat_bytes, data;
    uint32_t count;
};
Geometry geometry(uint64_t size, uint32_t cluster) {
    require(cluster >= 512 && cluster <= 524288 && (cluster & (cluster - 1)) == 0, "Invalid FATX cluster size.");
    uint64_t entries = size / cluster + 1;
    uint32_t bits = entries < 0xfff0 ? 16u : 32u;
    uint64_t fat = align4096(entries * (bits / 8));
    require(fat <= 128 * 1024 * 1024 && 4096 + fat < size, "FATX allocation table exceeds bound.");
    return {bits, fat, 4096 + fat, static_cast<uint32_t>((size - 4096 - fat) / cluster)};
}
class Scanner {
    Read read_;
    Layout layout_;
    Context& ctx_;
    Volume vol_;
    Geometry geom_{};
    Bytes fat_, used_;
    size_t entries_ = 0;
    uint32_t value(uint32_t c) const {
        size_t at = size_t(c) * (geom_.bits / 8);
        require(at + geom_.bits / 8 <= fat_.size(), "FAT access outside allocation table.");
        return geom_.bits == 16 ? le16(fat_.data() + at) : le32(fat_.data() + at);
    }
    uint64_t at(uint32_t c) const {
        require(c > 0 && c <= geom_.count, "FATX cluster outside volume.");
        return layout_.offset + geom_.data + uint64_t(c - 1) * vol_.cluster_bytes;
    }
    std::vector<uint32_t> chain(uint32_t first, const std::string& path) {
        std::vector<uint32_t> out;
        uint32_t c = first;
        for (;;) {
            ctx_.check();
            require(c > 0 && c <= geom_.count, "Out-of-range cluster at " + path);
            require(!used_[c], "Cross-linked or cyclic FATX chain at " + path);
            used_[c] = 1;
            out.push_back(c);
            uint32_t v = value(c);
            if (v >= (geom_.bits == 16 ? 0xfff8u : 0xfffffff8u))
                break;
            require(v > 0 && v < (geom_.bits == 16 ? 0xfff0u : 0xfffffff0u) && v <= geom_.count,
                    "Invalid or free cluster inside a live chain at " + path);
            c = v;
        }
        return out;
    }
    void directory(Node& node, const std::string& path, unsigned depth) {
        require(depth <= kMaxDepth, "FATX nesting exceeds 64 levels at " + path);
        std::set<std::string> names;
        Bytes b(vol_.cluster_bytes);
        bool ended = false;
        for (uint32_t c : node.chain) {
            if (ended)
                break;
            ctx_.check();
            read_(at(c), b.data(), b.size());
            for (size_t i = 0; i < b.size(); i += 64) {
                const uint8_t* p = b.data() + i;
                uint8_t len = p[0];
                if (len == 0 || len == 0xff) {
                    ended = true;
                    break;
                }
                if (len == 0xe5) {
                    ++vol_.deleted_entries;
                    continue;
                }
                require(len <= 42, "Invalid FATX filename length at " + path);
                require(++entries_ <= kMaxEntries, "FATX entry count exceeds safety limit.");
                Node child;
                child.name.assign(reinterpret_cast<const char*>(p + 2), len);
                require(child.name != "." && child.name != ".." && child.name.find('\0') == std::string::npos &&
                            child.name.find('/') == std::string::npos && child.name.find('\\') == std::string::npos,
                        "Unsafe FATX path token at " + path);
                require(names.insert(ascii_lower(child.name)).second, "Case-colliding/duplicate FATX name at " + path);
                child.attributes = p[1];
                require((child.attributes & 0xc8) == 0, "Unknown/unsupported FATX entry attributes at " + path);
                child.length = le32(p + 48);
                for (unsigned t = 0; t < 6; ++t)
                    child.times[t] = le16(p + 52 + t * 2);
                uint32_t first = le32(p + 44);
                const std::string full = path + "/" + child.name;
                if (first)
                    child.chain = chain(first, full);
                require(!child.directory() || !child.chain.empty(), "Directory has no cluster chain: " + full);
                if (child.directory())
                    directory(child, full, depth + 1);
                else
                    require(uint64_t(child.chain.size()) * vol_.cluster_bytes >= child.length,
                            "Truncated live-file chain: " + full);
                node.children.push_back(std::move(child));
            }
        }
    }

public:
    Scanner(Read r, Layout l, Context& c) : read_(std::move(r)), layout_(l), ctx_(c) {}
    Volume run() {
        vol_.layout = layout_;
        uint8_t header[16];
        read_(layout_.offset, header, sizeof(header));
        require(le32(header) == 0x58544146u, "Missing FATX volume at " + std::string(1, layout_.letter) +
                                                 ":. Unformatted/unknown partitions are not silently discarded.");
        vol_.volume_id = le32(header + 4);
        uint32_t spc = le32(header + 8);
        require(spc > 0 && spc <= 1024 && (spc & (spc - 1)) == 0, "Invalid sectors-per-cluster.");
        vol_.cluster_bytes = spc * 512;
        geom_ = geometry(layout_.size, vol_.cluster_bytes);
        vol_.fat_bits = geom_.bits;
        vol_.data_offset = geom_.data;
        vol_.cluster_count = geom_.count;
        fat_.resize(size_t(geom_.fat_bytes));
        read_(layout_.offset + 4096, fat_.data(), fat_.size());
        used_.assign(size_t(geom_.count) + 1, 0);
        vol_.root.name = std::string(1, layout_.letter);
        vol_.root.attributes = 0x10;
        vol_.root.chain = chain(le32(header + 12), vol_.root.name);
        directory(vol_.root, vol_.root.name, 0);
        for (uint32_t c = 1; c <= geom_.count; ++c) {
            if (!value(c))
                ++vol_.free_clusters;
            else
                require(used_[c] != 0, "Allocated but unreachable/bad FATX cluster " + std::to_string(c) + " in " +
                                           std::string(1, layout_.letter) +
                                           ":. Repair/recover the source separately; no data guessed away.");
        }
        return std::move(vol_);
    }
};
void each(Node& n, const std::function<void(Node&)>& fn) {
    fn(n);
    for (auto& c : n.children)
        each(c, fn);
}
void each_const(const Node& n, const std::string& path,
                const std::function<void(const Node&, const std::string&)>& fn) {
    fn(n, path);
    for (const auto& c : n.children)
        each_const(c, path + "/" + c.name, fn);
}
std::map<std::string, const Node*> flattened(const std::vector<Volume>& v) {
    std::map<std::string, const Node*> m;
    for (const auto& vol : v)
        each_const(vol.root, vol.root.name, [&](const Node& n, const std::string& p) {
            require(m.emplace(p, &n).second, "Duplicate inventory path.");
        });
    return m;
}
}
Volume scan_volume(const Read& r, const Layout& l, Context& ctx) { return Scanner(r, l, ctx).run(); }
void stream_node(const Read& read, const Volume& vol, const Node& node, const Sink& sink, Context& ctx) {
    require(!node.directory(), "Cannot stream a directory.");
    if (!node.replacement.empty()) {
        File file(absolute_safe(node.replacement, true));
        require(file.size() == node.length, "Staged payload size changed: " + utf8(node.replacement));
        Bytes chunk(kIOChunk);
        for (uint64_t pos = 0; pos < file.size();) {
            ctx.check();
            size_t count = size_t(std::min<uint64_t>(chunk.size(), file.size() - pos));
            file.read(pos, chunk.data(), count);
            sink(chunk.data(), count);
            pos += count;
        }
        file.stable();
        return;
    }
    Bytes b(std::min<size_t>(kIOChunk, vol.cluster_bytes));
    uint64_t remaining = node.length;
    for (auto c : node.chain) {
        if (!remaining)
            break;
        ctx.check();
        require(c > 0 && c <= vol.cluster_count, "Invalid stored file chain.");
        uint64_t offset = vol.layout.offset + vol.data_offset + uint64_t(c - 1) * vol.cluster_bytes;
        size_t n = size_t(std::min<uint64_t>(remaining, vol.cluster_bytes));
        read(offset, b.data(), n);
        sink(b.data(), n);
        remaining -= n;
    }
    require(remaining == 0, "Unexpected end of live file.");
}
void hash_volume(const Read& read, Volume& vol, Context& ctx) {
    each(vol.root, [&](Node& n) {
        if (n.directory())
            return;
        Sha256 h;
        stream_node(read, vol, n, [&](const uint8_t* p, size_t size) { h.add(p, size); }, ctx);
        n.digest = h.finish();
    });
}
void write_volume(File& out, uint64_t output_offset, const Read& source, const Volume& vol, bool cmp_identity,
                  Context& ctx) {
    Geometry g = geometry(vol.layout.size, kOutputCluster);
    struct Allocation {
        uint32_t first, count;
    };
    std::map<const Node*, Allocation> allocations;
    uint64_t next = 1;
    Bytes fat(size_t(g.fat_bytes), 0);
    auto setfat = [&](uint32_t c, uint32_t v) {
        if (g.bits == 16)
            put16(fat.data() + size_t(c) * 2, uint16_t(v));
        else
            put32(fat.data() + size_t(c) * 4, v);
    };
    setfat(0, 0xffffffffu);
    std::function<void(const Node&)> assign = [&](const Node& n) {
        ctx.check();
        uint64_t bytes = n.directory() ? (uint64_t(n.children.size()) + 1) * 64 : n.length;
        uint32_t count = static_cast<uint32_t>((bytes + kOutputCluster - 1) / kOutputCluster);
        uint32_t first = count ? static_cast<uint32_t>(next) : 0;
        require(next + count <= uint64_t(g.count) + 1,
                "Live files will not fit the supported Folder-HDD partition layout.");
        allocations.emplace(&n, Allocation{first, count});
        for (uint32_t i = 0; i < count; ++i)
            setfat(first + i, i + 1 < count ? first + i + 1 : 0xffffffffu);
        next += count;
        for (const auto& c : n.children)
            assign(c);
    };
    assign(vol.root);
    Bytes header(4096, 0xff);
    put32(header.data(), 0x58544146u);
    put32(header.data() + 4, cmp_identity ? (0x434d5000u | uint8_t(vol.layout.letter)) : vol.volume_id);
    put32(header.data() + 8, kOutputCluster / 512);
    put32(header.data() + 12, 1);
    out.write(output_offset, header.data(), header.size());
    out.write_sparse(output_offset + 4096, fat.data(), fat.size());
    std::function<void(const Node&)> write = [&](const Node& n) {
        ctx.check();
        auto a = allocations.at(&n);
        uint64_t off = output_offset + g.data + (a.first ? uint64_t(a.first - 1) * kOutputCluster : 0);
        if (n.directory()) {
            Bytes bytes(size_t(a.count) * kOutputCluster, 0xff);
            for (size_t i = 0; i < n.children.size(); ++i) {
                const auto& c = n.children[i];
                uint8_t* p = bytes.data() + i * 64;
                auto ca = allocations.at(&c);
                p[0] = static_cast<uint8_t>(c.name.size());
                p[1] = c.attributes;
                std::memcpy(p + 2, c.name.data(), c.name.size());
                put32(p + 44, ca.first);
                put32(p + 48, c.length);
                for (unsigned j = 0; j < 6; ++j)
                    put16(p + 52 + j * 2, c.times[j]);
            }
            out.write(off, bytes.data(), bytes.size());
            for (const auto& c : n.children)
                write(c);
        } else {
            Sha256 h;
            stream_node(
                source, vol, n,
                [&](const uint8_t* p, size_t size) {
                    h.add(p, size);
                    if (std::any_of(p, p + size, [](uint8_t v) { return v != 0; }))
                        out.write(off, p, size);
                    off += size;
                },
                ctx);
            require(h.finish() == n.digest, "Source payload changed while copying: " + n.name);
        }
    };
    write(vol.root);
}
void compare_volumes(const std::vector<Volume>& expected, const std::vector<Volume>& actual) {
    auto a = flattened(expected), b = flattened(actual);
    require(a.size() == b.size(), "Verification failed: entry counts differ.");
    for (const auto& item : a) {
        auto i = b.find(item.first);
        require(i != b.end(), "Verification failed: missing file/directory " + item.first);
        const auto& x = *item.second;
        const auto& y = *i->second;
        require(x.attributes == y.attributes && x.length == y.length && x.times == y.times && x.digest == y.digest,
                "Verification failed: file metadata/SHA-256 differs at " + item.first);
    }
}
void verify_clean_volume(File& out, const Volume& vol, Context& ctx) {
    // New writer packs all assigned clusters consecutively and recreates directory slack.
    std::vector<uint8_t> allocated(size_t(vol.cluster_count) + 1, 0);
    each_const(vol.root, vol.root.name, [&](const Node& n, const std::string&) {
        for (auto c : n.chain)
            allocated[c] = 1;
        if (!n.directory() && !n.chain.empty()) {
            uint64_t remaining = n.length;
            for (auto c : n.chain) {
                uint64_t used = std::min<uint64_t>(remaining, vol.cluster_bytes);
                uint64_t off = vol.layout.offset + vol.data_offset + uint64_t(c - 1) * vol.cluster_bytes;
                out.verify_zero(off + used, vol.cluster_bytes - used, ctx);
                remaining -= used;
            }
        }
        if (n.directory()) {
            uint64_t slots = 0;
            for (auto c : n.chain) {
                auto b = out.read(vol.layout.offset + vol.data_offset + uint64_t(c - 1) * vol.cluster_bytes,
                                  vol.cluster_bytes);
                for (size_t i = 0; i < b.size(); i += 64, ++slots) {
                    size_t start = slots < n.children.size() ? 2 + n.children[size_t(slots)].name.size() : 0;
                    size_t stop = slots < n.children.size() ? 44 : 64;
                    for (size_t j = start; j < stop; ++j)
                        require(b[i + j] == 0xff, "Directory slack contains data copied from source.");
                }
            }
        }
    });
    uint32_t c = 1;
    while (c <= vol.cluster_count) {
        if (allocated[c]) {
            ++c;
            continue;
        }
        uint32_t first = c;
        while (c <= vol.cluster_count && !allocated[c])
            ++c;
        out.verify_zero(vol.layout.offset + vol.data_offset + uint64_t(first - 1) * vol.cluster_bytes,
                        uint64_t(c - first) * vol.cluster_bytes, ctx);
    }
    uint64_t tail = vol.data_offset + uint64_t(vol.cluster_count) * vol.cluster_bytes;
    out.verify_zero(vol.layout.offset + tail, vol.layout.size - tail, ctx);
}
std::string inventory_json(const std::vector<Volume>& vols) {
    std::ostringstream s;
    s << "[";
    bool first = true;
    for (const auto& vol : vols)
        each_const(vol.root, vol.root.name, [&](const Node& n, const std::string& path) {
            if (!first)
                s << ",";
            first = false;
            s << "\n{\"path_hex\":" << quote_json(hex(path.data(), path.size()))
              << ",\"directory\":" << (n.directory() ? "true" : "false") << ",\"attributes\":" << unsigned(n.attributes)
              << ",\"size\":" << n.length << ",\"fatx_timestamps\":[";
            for (size_t i = 0; i < 6; ++i) {
                if (i)
                    s << ",";
                s << n.times[i];
            }
            s << "],\"sha256\":" << quote_json(n.digest) << "}";
        });
    s << "\n]";
    return s.str();
}
uint64_t payload_bytes(const std::vector<Volume>& vols) {
    uint64_t n = 0;
    for (const auto& v : vols)
        each_const(v.root, v.root.name, [&](const Node& x, const std::string&) {
            if (!x.directory())
                n += x.length;
        });
    return n;
}
uint64_t node_count(const std::vector<Volume>& vols) {
    uint64_t n = 0;
    for (const auto& v : vols)
        each_const(v.root, v.root.name, [&](const Node& x, const std::string&) {
            if (!x.directory())
                ++n;
        });
    return n;
}
}
