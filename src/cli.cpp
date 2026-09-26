// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/converter.hpp"
#include <csignal>
#include <iostream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace {
xhc::Context* running = nullptr;
void stop(int) {
    if (running)
        running->cancelled.store(true);
}
void help() {
    std::cout << R"(Xemu HDD Tools 0.6b - clean current-file conversions

  xemu-hdd-convert image-to-folder --source disk.qcow2 --output NewFolder --offline
  xemu-hdd-convert folder-to-image --source FolderHDD --output new.qcow2 --offline
  xemu-hdd-convert clean-image --source old.qcow2 --output clean.qcow2 --offline
  xemu-hdd-convert analyze --source disk.qcow2 --offline

Options:
  --qemu-img PATH       Trusted qemu-img helper (otherwise adjacent tools/ or PATH)
  --drop-snapshots      Acknowledge that ONLY CURRENT files are exported; original
                       snapshots remain in the untouched source, not the output.
  --raw-output         Advanced: output raw instead of QCOW2 (image destinations).
  --offline            Confirm XEMU and all other source writers are CLOSED.

Sources: standalone QCOW2, raw standard Xbox images, or synchronized TEST12
Folder-HDD. Standard C/E/X/Y/Z only. No F/G/custom partition table, encrypted or
backing-chain QCOW2, source repair, overwrite, live source, or mirror guessing.
Every output is a fresh filesystem: deleted entries/free contents/file slack/
directory slack are not copied. Live cache and recovery files ARE preserved.
Required system metadata is preserved separately. The program is not a forensic
or save-state backup. Failures retain an explicitly reported workspace.
)";
}
int run(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        help();
        return 0;
    }
    if (args[0] == "--version") {
        std::cout << "Xemu HDD Tools 0.6b\n";
        return 0;
    }
    xhc::Options o;
    if (args[0] == "image-to-folder")
        o.mode = xhc::Mode::ImageToFolder;
    else if (args[0] == "folder-to-image")
        o.mode = xhc::Mode::FolderToImage;
    else if (args[0] == "clean-image")
        o.mode = xhc::Mode::CleanImage;
    else if (args[0] == "analyze")
        o.mode = xhc::Mode::Analyze;
    else
        throw xhc::Error("Unknown command: " + args[0]);
    for (size_t i = 1; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--offline")
            o.offline_confirmed = true;
        else if (a == "--drop-snapshots")
            o.acknowledge_snapshot_exclusion = true;
        else if (a == "--raw-output")
            o.raw_output = true;
        else {
            if (a != "--source" && a != "--output" && a != "--qemu-img")
                throw xhc::Error("Unknown option: " + a);
            xhc::require(i + 1 < args.size(), "Missing value for " + a);
            auto p = std::filesystem::u8path(args[++i]);
            if (a == "--source")
                o.source = p;
            else if (a == "--output")
                o.destination = p;
            else
                o.qemu_img = p;
        }
    }
    xhc::Context ctx;
    ctx.log = [](const std::string& line) { std::cerr << line << '\n'; };
    running = &ctx;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    try {
        auto r = xhc::convert(o, ctx);
        running = nullptr;
        std::cout << "Verified files: " << r.files << "\nLive payload bytes: " << r.live_bytes << "\n";
        return 0;
    } catch (...) {
        running = nullptr;
        throw;
    }
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(std::filesystem::path(argv[i]).u8string());
#else
int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
#endif
    try {
        return run(args);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
