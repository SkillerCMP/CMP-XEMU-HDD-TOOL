// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/workspace.hpp"
namespace xhc::ui {
SourceChange Workspace::source(const fs::path& path, Format format, bool dirty, bool discard) {
    if (busy())
        return SourceChange::Busy;
    if (settings_.source == path && settings_.source_format == format)
        return SourceChange::Unchanged;
    if (dirty && !discard)
        return SourceChange::NeedsDiscard;
    settings_.source = path;
    settings_.source_format = format;
    ++revision_;
    return SourceChange::Applied;
}
bool Workspace::output(const fs::path& path, Format format) {
    if (busy())
        return false;
    settings_.output = path;
    settings_.output_format = format;
    return true;
}
bool Workspace::helpers(const fs::path& qemu, const fs::path& ffmpeg) {
    if (busy())
        return false;
    settings_.qemu_img = qemu;
    settings_.ffmpeg = ffmpeg;
    return true;
}
Ticket Workspace::begin() {
    require(!busy(), "Another operation owns the shared HDD workspace.");
    active_ = ++next_job_;
    return {active_, revision_, settings_};
}
bool Workspace::finish(uint64_t job) {
    if (!job || active_ != job)
        return false;
    active_ = 0;
    return true;
}
Mode conversion_mode(Format source, Format output, bool verify) {
    if (verify)
        return Mode::Analyze;
    if (output == Format::Folder)
        return source == Format::Folder ? Mode::RebuildFolder : Mode::ImageToFolder;
    return source == Format::Folder ? Mode::FolderToImage : Mode::CleanImage;
}
Options disk_options(const Settings& s, bool verify) {
    Options o;
    o.source = s.source;
    o.destination = verify ? fs::path{} : s.output;
    o.qemu_img = s.qemu_img;
    o.mode = conversion_mode(s.source_format, s.output_format, verify);
    o.raw_output = s.output_format == Format::Raw;
    o.expected_source = s.source_format == Format::Folder ? SourceKind::Folder
                        : s.source_format == Format::Raw  ? SourceKind::Raw
                                                          : SourceKind::Qcow2;
    return o;
}
bool output_enabled(bool busy, unsigned active_tab, bool verify) { return !busy && !(active_tab == 0 && verify); }
const char* format_name(Format format) {
    switch (format) {
    case Format::Qcow2:
        return "QCOW2";
    case Format::Folder:
        return "Folder-HDD";
    case Format::Raw:
        return "RAW";
    }
    throw Error("Unknown format selection.");
}
}
