// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "converter.hpp"
#include <set>
#include <optional>
#include "csb_format.hh"

namespace xhc::csb {
using XemuCsb::StDbTrack;
using XemuCsb::StDbSoundtrack;
using XemuCsb::StDbMetadata;
struct Soundtrack {
    StDbSoundtrack record;
    uint64_t bytes = 0;
    bool orphan = false;
    bool missing = false;
};
struct Catalog {
    fs::path source;
    std::string fingerprint;
    StDbMetadata metadata;
    std::vector<Soundtrack> soundtracks;
    std::set<uint16_t> reserved_serials;
    bool writable = true;
    bool has_database = false;
    std::string warning;
};
struct Row {
    StDbTrack track;
    fs::path local_file; // Empty means retain the existing HDD file.
    bool retained() const { return local_file.empty(); }
};
struct Edit {
    uint16_t id = 0;
    std::string name;
    bool existing = false;
    bool delete_soundtrack = false;
    std::vector<Row> rows;
};
class Editor {
    Catalog catalog_;
    Edit edit_;
    bool selected_ = false, read_only_ = false, dirty_ = false;
    uint64_t generation_ = 1;

public:
    const Catalog& catalog() const { return catalog_; }
    const Edit& edit() const { return edit_; }
    bool selected() const { return selected_; }
    bool read_only() const { return read_only_ || !catalog_.writable; }
    bool dirty() const { return dirty_; }
    uint64_t generation() const { return generation_; }
    bool can_save() const;
    void set_catalog(Catalog catalog); // Caller explicitly resolves dirty edits first.
    void select(uint16_t id);
    void new_soundtrack();
    void rename_soundtrack(const std::string& name);
    void rename_track(size_t index, const std::string& title);
    void add_files(const std::vector<fs::path>& paths);
    void remove_track(size_t index);
    void clear();
    bool reorder(size_t from, size_t boundary, uint64_t generation);
};
struct SaveOptions {
    Options disk;
    bool folder_output = false;
    fs::path ffmpeg;
    // Backups always reside in destination.parent_path()/Backups/CSB-<unique>.
};
struct Saved {
    Result disk;
    fs::path backup;
};
Catalog read_catalog(const Options& source, Context& ctx);
Saved save(const Catalog& loaded, const Edit& edit, const SaveOptions& options, Context& ctx);
fs::path default_ffmpeg_path();
fs::path find_ffmpeg(const fs::path& requested);
std::string duration(uint32_t milliseconds);
std::string title_from_path(const fs::path& path);
void validate_name(const std::string& text, size_t max_utf16_units);
// Snapshot adapter for validated offline volume data.
Catalog catalog_from_snapshot(const std::vector<Volume>& volumes, const Read& read, const Bytes& system,
                              const fs::path& source, Context& ctx);
}
