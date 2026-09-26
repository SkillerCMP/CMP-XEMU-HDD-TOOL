// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "converter.hpp"

namespace xhc::ui {
enum class Format { Qcow2, Folder, Raw };
enum class SourceChange { Unchanged, Busy, NeedsDiscard, Applied };
struct Settings {
    fs::path source, output, qemu_img, ffmpeg;
    Format source_format = Format::Qcow2, output_format = Format::Qcow2;
};
struct Ticket {
    uint64_t id = 0, source_revision = 0;
    Settings settings;
};
// UI-thread-owned state. Workers receive immutable tickets/options, never this object.
class Workspace {
    Settings settings_;
    uint64_t revision_ = 1, next_job_ = 0, active_ = 0;

public:
    const Settings& settings() const { return settings_; }
    uint64_t revision() const { return revision_; }
    bool busy() const { return active_ != 0; }
    uint64_t active_job() const { return active_; }
    SourceChange source(const fs::path& path, Format format, bool dirty, bool discard);
    bool output(const fs::path& path, Format format);
    bool helpers(const fs::path& qemu, const fs::path& ffmpeg);
    Ticket begin();
    bool finish(uint64_t job);
};
Mode conversion_mode(Format source, Format output, bool verify);
Options disk_options(const Settings& settings, bool verify);
bool output_enabled(bool busy, unsigned active_tab, bool verify);
const char* format_name(Format format);
}
