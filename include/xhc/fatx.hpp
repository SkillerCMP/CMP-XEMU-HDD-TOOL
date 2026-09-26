// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common.hpp"

namespace xhc {
struct Layout {
    char letter;
    uint64_t offset;
    uint64_t size;
};
extern const std::array<Layout, 5> kLayout;
struct Node {
    std::string name;
    uint8_t attributes = 0;
    uint32_t length = 0;
    std::array<uint16_t, 6> times{};
    std::vector<uint32_t> chain;
    std::vector<Node> children;
    std::string digest;
    fs::path replacement; // Private, immutable host payload for an offline edit only.
    bool directory() const { return (attributes & 0x10) != 0; }
};
struct Volume {
    Layout layout{};
    uint32_t volume_id = 0;
    uint32_t cluster_bytes = 0;
    uint32_t fat_bits = 0;
    uint64_t data_offset = 0;   // relative to this volume
    uint32_t cluster_count = 0; // number of complete data clusters
    uint64_t deleted_entries = 0;
    uint64_t free_clusters = 0;
    Node root;
};
using Read = std::function<void(uint64_t, void*, size_t)>;
using Sink = std::function<void(const uint8_t*, size_t)>;
Volume scan_volume(const Read& read, const Layout& layout, Context& ctx);
void stream_node(const Read& read, const Volume& vol, const Node& node, const Sink& sink, Context& ctx);
void hash_volume(const Read& read, Volume& vol, Context& ctx);
void write_volume(File& output, uint64_t offset, const Read& source, const Volume& vol, bool cmp_identity,
                  Context& ctx);
void compare_volumes(const std::vector<Volume>& expected, const std::vector<Volume>& actual);
void verify_clean_volume(File& output, const Volume& vol, Context& ctx);
std::string inventory_json(const std::vector<Volume>& vols);
uint64_t payload_bytes(const std::vector<Volume>& vols);
uint64_t node_count(const std::vector<Volume>& vols);
}
