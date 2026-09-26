// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace xhc {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Cancelled : Error {
    Cancelled() : Error("Cancelled; source was not modified.") {}
};
struct Context {
    std::atomic_bool cancelled{false};
    std::function<void(const std::string&)> log;
    void check() const {
        if (cancelled.load())
            throw Cancelled();
    }
    void say(const std::string& s) const {
        check();
        if (log)
            log(s);
    }
};
constexpr uint64_t kDiskSize = 0x1DD156000ULL;
constexpr uint64_t kSystemArea = 0x80000;
constexpr uint64_t kCacheEnd = 0x8CA80000;
constexpr uint32_t kOutputCluster = 16384;
constexpr size_t kIOChunk = 1024 * 1024;
constexpr size_t kMaxEntries = 200000;
constexpr unsigned kMaxDepth = 64;
uint16_t le16(const uint8_t* p);
uint32_t le32(const uint8_t* p);
uint64_t be64(const uint8_t* p);
uint32_t be32(const uint8_t* p);
void put16(uint8_t* p, uint16_t x);
void put32(uint8_t* p, uint32_t x);
std::string hex(const void* data, size_t n);
std::string hex64(uint64_t x);
std::string ascii_lower(std::string s);
std::string utf8(const fs::path& p);
std::string b64(const std::string& s);
std::string unb64(const std::string& s);
std::string quote_json(const std::string& s);
void require(bool v, const std::string& message);
void validate_host_component(const std::string& name);
void reject_links(const fs::path& path);
fs::path absolute_safe(const fs::path& path, bool must_exist);
bool is_within(const fs::path& path, const fs::path& parent);
void move_new(const fs::path& from, const fs::path& to);
void sync_directory(const fs::path& path);
uint64_t allocated_bytes(const fs::path& path);
int64_t unix_mtime(const fs::path& path);
std::string read_text(const fs::path& path, size_t limit = 16 * 1024 * 1024);
void write_text_new(const fs::path& path, const std::string& text);
std::string random_token();

class Sha256 {
    std::array<uint32_t, 8> state_{
        {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
    std::array<uint8_t, 64> buffer_{};
    uint64_t bytes_ = 0;
    size_t used_ = 0;
    void block(const uint8_t* p);

public:
    void add(const void* p, size_t n);
    std::string finish();
};

// Local regular files only. Source handles are read-only; new outputs use exclusive creation.
class File {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    File(const fs::path& path, bool create = false, uint64_t length = 0, bool source_lock = true);
    ~File();
    File(File&&) noexcept;
    File& operator=(File&&) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    uint64_t size() const;
    const fs::path& path() const;
    void read(uint64_t offset, void* dst, size_t n) const;
    Bytes read(uint64_t offset, size_t n) const;
    void write(uint64_t offset, const void* src, size_t n);
    void write_sparse(uint64_t offset, const void* src, size_t n);
    void flush();
    void stable() const;
    void verify_zero(uint64_t offset, uint64_t length, Context& ctx) const;
};
std::string hash_file(File& file, Context& ctx);

// Minimal bounded JSON value used for qemu-img's machine-readable results.
struct Json {
    enum class Kind { Null, Boolean, Number, String, Array, Object } kind = Kind::Null;
    std::string scalar;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    bool contains(const std::string& key) const;
    const Json& at(const std::string& key) const;
    std::string text() const;
    uint64_t u64() const;
    bool boolean() const;
    static Json parse(const std::string& text);
};

struct ProcessResult {
    int code = -1;
    std::string output;
};
std::wstring quote_windows_argument(const std::wstring& arg);
ProcessResult run_process(const fs::path& executable, const std::vector<std::string>& args, Context& ctx);
fs::path executable_directory();
fs::path find_qemu(const fs::path& requested);
}
