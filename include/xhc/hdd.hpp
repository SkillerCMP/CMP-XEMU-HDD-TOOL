// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "converter.hpp"

namespace xhc::hdd {
// A read-only snapshot. Edits below are pending until saved to a NEW output.
struct Catalog {
    fs::path source;
    std::string fingerprint;
    std::vector<Volume> volumes;
};
struct Path {
    char partition = 'E';
    std::vector<std::string> components;
    static Path parse(const std::string& text);
    std::string str() const;
    Path parent() const;
    Path child(const std::string& name) const;
    bool root() const { return components.empty(); }
};
const Volume& volume(const std::vector<Volume>& volumes, char letter);
const Node& lookup(const std::vector<Volume>& volumes, const Path& path);
std::string snapshot_fingerprint(const std::vector<Volume>& volumes, const Bytes& system);
Catalog read_catalog(const Options& source, Context& ctx);
class Editor {
    Catalog original_;
    std::vector<Volume> working_;
    bool dirty_ = false;
    uint64_t generation_ = 0;
    void commit(std::vector<Volume> working);

public:
    void load(Catalog catalog);
    const Catalog& original() const { return original_; }
    const std::vector<Volume>& volumes() const { return working_; }
    bool loaded() const { return !original_.fingerprint.empty(); }
    bool dirty() const { return dirty_; }
    uint64_t generation() const { return generation_; }
    void discard();
    void new_folder(const Path& parent, const std::string& name);
    void rename(const Path& path, const std::string& name);
    void remove(const Path& path);
    void copy_move(const Path& source, const Path& destination_folder, bool move);
    // Hashes host files; call on a worker, not in a paint/drop callback.
    void import_paths(const Path& destination_folder, const std::vector<fs::path>& paths, Context& ctx);
    void replace_file(const Path& path, const fs::path& local, Context& ctx);
};
struct SaveOptions {
    Options disk;
    bool folder_output = false;
};
struct Saved {
    Result disk;
    fs::path backup;
};
Saved save(const Editor& editor, const SaveOptions& options, Context& ctx);
// Export reads the unedited source. Existing destinations are never merged/overwritten.
fs::path export_items(const Catalog& catalog, const Options& source, const std::vector<Path>& paths,
                      const fs::path& destination, Context& ctx);
std::string modified_time(const Node& node);
std::string attributes(uint8_t value);
}
