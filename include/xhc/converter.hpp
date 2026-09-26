// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "fatx.hpp"
namespace xhc {
enum class SourceKind { Auto, Qcow2, Folder, Raw };
enum class Mode { Analyze, ImageToFolder, FolderToImage, CleanImage, RebuildFolder };
// Offline content adapters see only a validated, hashed snapshot. No live guest writes.
struct ContentHooks {
    std::function<void(const std::vector<Volume>&, const Read&, const Bytes&, Context&)> inspect;
    std::function<void(std::vector<Volume>&, const Read&, const fs::path&, Context&)> edit;
};
struct Options {
    Mode mode = Mode::Analyze;
    fs::path source;
    fs::path destination;
    fs::path qemu_img;
    bool offline_confirmed = false;
    bool acknowledge_snapshot_exclusion = false;
    SourceKind expected_source = SourceKind::Auto; // CLI auto-detection remains the default.
    // Optional UI consent, invoked only for actual snapshots on a write operation.
    std::function<bool(uint64_t)> confirm_snapshot_exclusion;
    std::shared_ptr<ContentHooks> content_hooks;
    fs::path workspace_parent; // Optional private host-side transaction root.
    bool raw_output = false;   // Advanced export; default output is QCOW2
};
struct Result {
    fs::path destination;
    fs::path report;
    uint64_t files = 0;
    uint64_t live_bytes = 0;
};
bool allow_snapshot_exclusion(const Options& options, uint64_t snapshots, Context& ctx);
Result convert(const Options& options, Context& ctx);
}
