// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/common.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace xhc {
void require(bool v, const std::string& m) {
    if (!v)
        throw Error(m);
}
uint16_t le16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint32_t be32(const uint8_t* p) {
    return uint32_t(p[3]) | uint32_t(p[2]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[0]) << 24;
}
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
void put16(uint8_t* p, uint16_t x) {
    p[0] = uint8_t(x);
    p[1] = uint8_t(x >> 8);
}
void put32(uint8_t* p, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = uint8_t(x >> (i * 8));
}
std::string hex(const void* data, size_t n) {
    static const char* a = "0123456789ABCDEF";
    auto p = static_cast<const uint8_t*>(data);
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += a[p[i] >> 4];
        s += a[p[i] & 15];
    }
    return s;
}
std::string hex64(uint64_t x) {
    std::ostringstream s;
    s << std::hex << std::uppercase << x;
    return s.str();
}
std::string ascii_lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}
std::string utf8(const fs::path& p) { return p.u8string(); }
std::string b64(const std::string& s) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < s.size(); i += 3) {
        uint32_t x = uint32_t(uint8_t(s[i])) << 16;
        if (i + 1 < s.size())
            x |= uint32_t(uint8_t(s[i + 1])) << 8;
        if (i + 2 < s.size())
            x |= uint8_t(s[i + 2]);
        out += t[(x >> 18) & 63];
        out += t[(x >> 12) & 63];
        out += i + 1 < s.size() ? t[(x >> 6) & 63] : '=';
        out += i + 2 < s.size() ? t[x & 63] : '=';
    }
    return out;
}
std::string unb64(const std::string& s) {
    require(s.size() % 4 == 0, "Invalid base64 length in synchronization manifest.");
    std::string out;
    uint32_t v = 0;
    int bits = 0;
    bool pad = false;
    for (char c : s) {
        if (c == '=') {
            pad = true;
            continue;
        }
        require(!pad, "Invalid base64 padding.");
        int x = c >= 'A' && c <= 'Z'   ? c - 'A'
                : c >= 'a' && c <= 'z' ? c - 'a' + 26
                : c >= '0' && c <= '9' ? c - '0' + 52
                : c == '+'             ? 62
                : c == '/'             ? 63
                                       : -1;
        require(x >= 0, "Invalid base64 character.");
        v = (v << 6) | uint32_t(x);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((v >> bits) & 255);
        }
    }
    require(b64(out) == s, "Noncanonical base64 in synchronization manifest.");
    return out;
}
std::string quote_json(const std::string& s) {
    std::string out = "\"";
    static const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 32) {
            out += "\\u00";
            out += h[c >> 4];
            out += h[c & 15];
        } else
            out += char(c);
    }
    return out + '"';
}
void validate_host_component(const std::string& name) {
    require(!name.empty() && name.size() <= 42 && name != "." && name != "..", "Invalid FATX filename.");
    for (unsigned char c : name)
        require(c >= 32 && c < 127 && std::string("<>:\"/\\|?*").find(char(c)) == std::string::npos,
                "Filename cannot be represented unchanged in the portable host mirror (hex=" +
                    hex(name.data(), name.size()) + "). No files renamed.");
    require(name.back() != '.' && name.back() != ' ', "Trailing-dot/space filename is not portable: " + name);
    auto stem = ascii_lower(name.substr(0, name.find('.')));
    require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" && stem != "clock$" &&
                !(stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '0' &&
                  stem[3] <= '9'),
            "Reserved Windows filename: " + name);
}
void reject_links(const fs::path& p) {
    std::error_code ec;
    auto st = fs::symlink_status(p, ec);
    require(!ec, "Cannot examine path: " + utf8(p) + ": " + ec.message());
    require(!fs::is_symlink(st), "Symbolic links are not accepted: " + utf8(p));
#ifdef _WIN32
    DWORD attr = GetFileAttributesW(p.c_str());
    require(attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_REPARSE_POINT),
            "Reparse points are not accepted: " + utf8(p));
#endif
}
fs::path absolute_safe(const fs::path& p, bool must_exist) {
    require(!p.empty(), "An input/output path is required.");
    auto a = fs::absolute(p).lexically_normal();
    // Keep drive/UNC roots intact. A Windows root-name such as "Z:" is drive-relative
    // and must not be probed separately from its root directory.
    fs::path cur = a.root_path();
    if (!cur.empty()) {
        std::error_code ec;
        auto st = fs::symlink_status(cur, ec);
        require(!ec && fs::exists(st), "Missing or inaccessible path: " + utf8(cur));
        reject_links(cur);
    }
    for (const auto& component : a.relative_path()) {
        cur /= component;
        std::error_code ec;
        auto st = fs::symlink_status(cur, ec);
        if (!ec && fs::exists(st))
            reject_links(cur);
        else {
            require(!must_exist && cur == a, "Missing or inaccessible path: " + utf8(cur));
        }
    }
    if (must_exist)
        require(fs::exists(a), "Missing source: " + utf8(a));
    else
        require(!fs::exists(fs::symlink_status(a)),
                "Destination already exists; overwrite is not supported: " + utf8(a));
    return a;
}
bool is_within(const fs::path& p, const fs::path& parent) {
    auto a = p.lexically_normal(), b = parent.lexically_normal();
    auto i = a.begin(), j = b.begin();
    for (; j != b.end(); ++j, ++i) {
        if (i == a.end())
            return false;
#ifdef _WIN32
        if (CompareStringOrdinal(i->native().c_str(), -1, j->native().c_str(), -1, TRUE) != CSTR_EQUAL)
            return false;
#else
        if (*i != *j)
            return false;
#endif
    }
    return true;
}
void sync_directory(const fs::path& p) {
#ifndef _WIN32
    int fd = open(p.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    require(fd >= 0, "Cannot open directory for durable flush: " + utf8(p));
    int r = fsync(fd);
    int e = errno;
    close(fd);
    require(r == 0, "Directory flush failed: " + std::string(std::strerror(e)));
#else
    (void)p; // Files are flushed explicitly; MoveFileExW uses WRITE_THROUGH.
#endif
}
void move_new(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    const BOOL moved = MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH);
    const DWORD move_error = GetLastError();
    require(moved != 0, "Cannot publish without replacing an existing path: " + utf8(to) + " (Win32 " +
                            std::to_string(move_error) + ")");
#else
#ifdef SYS_renameat2
    const long moved = syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1);
    const int move_error = errno;
    require(moved == 0, "Cannot publish without replacement: " + utf8(to) + ": " + std::strerror(move_error));
#else
    require(!fs::is_directory(from), "Atomic directory publication is unavailable on this platform.");
    require(link(from.c_str(), to.c_str()) == 0, "Exclusive publication failed: " + utf8(to));
    require(unlink(from.c_str()) == 0, "Published file but staging unlink failed: " + utf8(from));
#endif
    sync_directory(to.parent_path());
#endif
}
std::string random_token() {
    std::random_device rd;
    std::array<uint32_t, 4> a{{rd(), rd(), rd(), rd()}};
    return hex(a.data(), sizeof(a));
}

static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
void Sha256::block(const uint8_t* p) {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = be32(p + 4 * i);
    for (unsigned i = 16; i < 64; ++i) {
        auto x = w[i - 15], y = w[i - 2];
        w[i] = w[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + w[i - 7] +
               (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
    }
    auto a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6],
         h = state_[7];
    for (unsigned i = 0; i < 64; ++i) {
        auto t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        auto t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}
void Sha256::add(const void* ptr, size_t n) {
    auto p = static_cast<const uint8_t*>(ptr);
    bytes_ += n;
    while (n) {
        size_t take = std::min(n, 64 - used_);
        std::memcpy(buffer_.data() + used_, p, take);
        used_ += take;
        p += take;
        n -= take;
        if (used_ == 64) {
            block(buffer_.data());
            used_ = 0;
        }
    }
}
std::string Sha256::finish() {
    uint64_t bits = bytes_ * 8;
    uint8_t x = 0x80;
    add(&x, 1);
    x = 0;
    while (used_ != 56)
        add(&x, 1);
    uint8_t b[8];
    for (unsigned i = 0; i < 8; ++i)
        b[7 - i] = uint8_t(bits >> (i * 8));
    add(b, 8);
    uint8_t out[32];
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 4; ++j)
            out[4 * i + j] = uint8_t(state_[i] >> (24 - 8 * j));
    return hex(out, 32);
}

struct File::Impl {
    fs::path path;
    bool created = false;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION initial{};
    ~Impl() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    int fd = -1;
    struct stat initial{};
    ~Impl() {
        if (fd >= 0)
            close(fd);
    }
#endif
};
#ifndef _WIN32
static void reject_writers(const fs::path& path, const struct stat& target) {
#ifdef __linux__
    std::error_code ec;
    for (auto it = fs::directory_iterator("/proc", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        auto pid = it->path().filename().string();
        if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](char c) { return c >= '0' && c <= '9'; }))
            continue;
        std::error_code e2;
        for (auto fd = fs::directory_iterator(it->path() / "fd", e2); !e2 && fd != fs::directory_iterator();
             fd.increment(e2)) {
            struct stat st{};
            if (stat(fd->path().c_str(), &st) != 0 || st.st_dev != target.st_dev || st.st_ino != target.st_ino)
                continue;
            std::ifstream info(it->path() / "fdinfo" / fd->path().filename());
            std::string line;
            while (std::getline(info, line))
                if (line.rfind("flags:", 0) == 0) {
                    std::istringstream s(line.substr(6));
                    unsigned flags = 0;
                    s >> std::oct >> flags;
                    require((flags & O_ACCMODE) == O_RDONLY, "Source has an open writable handle (PID " + pid + "): " +
                                                                 utf8(path) + ". Close XEMU and disk tools first.");
                }
        }
    }
#else
    (void)path;
    (void)target;
#endif
}
#endif
File::File(const fs::path& p, bool create, uint64_t length, bool source_lock) : impl_(std::make_unique<Impl>()) {
    impl_->path = p;
    impl_->created = create;
#ifdef _WIN32
    impl_->handle =
        CreateFileW(p.c_str(), create ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ, FILE_SHARE_READ, nullptr,
                    create ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const DWORD open_error = GetLastError();
    require(impl_->handle != INVALID_HANDLE_VALUE,
            "Cannot open file exclusively/read-only: " + utf8(p) + " (Win32 " + std::to_string(open_error) + ")");
    require(GetFileInformationByHandle(impl_->handle, &impl_->initial) != 0 &&
                !(impl_->initial.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
            "Not a regular local file: " + utf8(p));
    if (create) {
        DWORD ignored = 0;
        DeviceIoControl(impl_->handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ignored, nullptr);
        LARGE_INTEGER off{};
        off.QuadPart = static_cast<LONGLONG>(length);
        require(SetFilePointerEx(impl_->handle, off, nullptr, FILE_BEGIN) && SetEndOfFile(impl_->handle),
                "Cannot size new sparse output: " + utf8(p));
    }
    (void)source_lock;
#else
    impl_->fd = open(p.c_str(), (create ? (O_RDWR | O_CREAT | O_EXCL) : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(impl_->fd >= 0, "Cannot open file: " + utf8(p) + ": " + std::strerror(errno));
    require(fstat(impl_->fd, &impl_->initial) == 0 && S_ISREG(impl_->initial.st_mode),
            "Not a regular file: " + utf8(p));
    if (create)
        require(length <= uint64_t(std::numeric_limits<off_t>::max()) &&
                    ftruncate(impl_->fd, static_cast<off_t>(length)) == 0,
                "Cannot size new sparse output: " + utf8(p));
    else if (source_lock) {
        reject_writers(p, impl_->initial);
        require(flock(impl_->fd, LOCK_SH | LOCK_NB) == 0, "Source is locked by another process: " + utf8(p));
    }
#endif
}
File::~File() = default;
File::File(File&&) noexcept = default;
File& File::operator=(File&&) noexcept = default;
const fs::path& File::path() const { return impl_->path; }
uint64_t File::size() const {
#ifdef _WIN32
    LARGE_INTEGER sz{};
    require(GetFileSizeEx(impl_->handle, &sz) != 0 && sz.QuadPart >= 0, "Cannot read file size.");
    return uint64_t(sz.QuadPart);
#else
    struct stat st{};
    require(fstat(impl_->fd, &st) == 0 && st.st_size >= 0, "Cannot read file size.");
    return uint64_t(st.st_size);
#endif
}
void File::read(uint64_t offset, void* dst, size_t n) const {
    auto sz = size();
    require(offset <= sz && uint64_t(n) <= sz - offset,
            "Read beyond end of " + utf8(path()) + " at 0x" + hex64(offset));
    auto* p = static_cast<uint8_t*>(dst);
#ifdef _WIN32
    LARGE_INTEGER off{};
    off.QuadPart = static_cast<LONGLONG>(offset);
    require(SetFilePointerEx(impl_->handle, off, nullptr, FILE_BEGIN) != 0, "Seek failed.");
    while (n) {
        DWORD got = 0, chunk = static_cast<DWORD>(std::min(n, kIOChunk));
        require(ReadFile(impl_->handle, p, chunk, &got, nullptr) && got == chunk, "Read failed: " + utf8(path()));
        p += got;
        n -= got;
    }
#else
    while (n) {
        size_t chunk = std::min(n, kIOChunk);
        ssize_t got = pread(impl_->fd, p, chunk, static_cast<off_t>(offset));
        if (got < 0 && errno == EINTR)
            continue;
        require(got > 0, "Read failed: " + utf8(path()));
        p += got;
        n -= size_t(got);
        offset += uint64_t(got);
    }
#endif
}
Bytes File::read(uint64_t off, size_t n) const {
    Bytes v(n);
    read(off, v.data(), n);
    return v;
}
void File::write(uint64_t offset, const void* src, size_t n) {
    require(impl_->created, "Attempted source modification.");
    auto sz = size();
    require(offset <= sz && uint64_t(n) <= sz - offset, "Write beyond output length.");
    auto p = static_cast<const uint8_t*>(src);
#ifdef _WIN32
    LARGE_INTEGER off{};
    off.QuadPart = static_cast<LONGLONG>(offset);
    require(SetFilePointerEx(impl_->handle, off, nullptr, FILE_BEGIN) != 0, "Output seek failed.");
    while (n) {
        DWORD done = 0, chunk = static_cast<DWORD>(std::min(n, kIOChunk));
        require(WriteFile(impl_->handle, p, chunk, &done, nullptr) && done == chunk,
                "Output write failed: " + utf8(path()));
        p += done;
        n -= done;
    }
#else
    while (n) {
        ssize_t done = pwrite(impl_->fd, p, std::min(n, kIOChunk), static_cast<off_t>(offset));
        if (done < 0 && errno == EINTR)
            continue;
        require(done > 0, "Output write failed: " + utf8(path()));
        p += done;
        n -= size_t(done);
        offset += uint64_t(done);
    }
#endif
}
void File::write_sparse(uint64_t offset, const void* src, size_t n) {
    require(impl_->created, "Sparse writes are only allowed on fresh output files.");
    const uint64_t length = size();
    require(offset <= length && uint64_t(n) <= length - offset, "Sparse write exceeds fresh output.");
    const auto* data = static_cast<const uint8_t*>(src);
    while (n) {
        const size_t chunk = std::min<size_t>(n, 4096);
        if (std::any_of(data, data + chunk, [](uint8_t byte) { return byte != 0; }))
            write(offset, data, chunk);
        data += chunk;
        offset += chunk;
        n -= chunk;
    }
}
void File::flush() {
#ifdef _WIN32
    require(FlushFileBuffers(impl_->handle) != 0, "Durable output flush failed: " + utf8(path()));
#else
    require(fsync(impl_->fd) == 0, "Durable output flush failed: " + utf8(path()));
#endif
}
void File::stable() const {
    require(!impl_->created, "Stability check must use a source handle.");
    reject_links(path());
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION now{};
    require(GetFileInformationByHandle(impl_->handle, &now) != 0, "Cannot recheck source.");
    const auto& old = impl_->initial;
    HANDLE entry = CreateFileW(path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(entry != INVALID_HANDLE_VALUE, "Source path cannot be rechecked: " + utf8(path()));
    BY_HANDLE_FILE_INFORMATION path_info{};
    const BOOL got_path_info = GetFileInformationByHandle(entry, &path_info);
    CloseHandle(entry);
    require(got_path_info && path_info.dwVolumeSerialNumber == old.dwVolumeSerialNumber &&
                path_info.nFileIndexHigh == old.nFileIndexHigh && path_info.nFileIndexLow == old.nFileIndexLow &&
                !(path_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
            "Source path identity changed during conversion: " + utf8(path()));
    require(now.nFileSizeHigh == old.nFileSizeHigh && now.nFileSizeLow == old.nFileSizeLow &&
                now.ftLastWriteTime.dwLowDateTime == old.ftLastWriteTime.dwLowDateTime &&
                now.ftLastWriteTime.dwHighDateTime == old.ftLastWriteTime.dwHighDateTime,
            "Source changed during conversion: " + utf8(path()));
#else
    struct stat now{}, entry{};
    require(fstat(impl_->fd, &now) == 0 && stat(path().c_str(), &entry) == 0, "Cannot recheck source.");
    const auto& old = impl_->initial;
    require(now.st_dev == entry.st_dev && now.st_ino == entry.st_ino && now.st_size == old.st_size &&
                now.st_mtim.tv_sec == old.st_mtim.tv_sec && now.st_mtim.tv_nsec == old.st_mtim.tv_nsec &&
                now.st_ctim.tv_sec == old.st_ctim.tv_sec && now.st_ctim.tv_nsec == old.st_ctim.tv_nsec,
            "Source changed during conversion: " + utf8(path()));
#endif
}
uint64_t allocated_bytes(const fs::path& p) {
#ifdef _WIN32
    DWORD hi = 0;
    SetLastError(NO_ERROR);
    DWORD lo = GetCompressedFileSizeW(p.c_str(), &hi);
    require(lo != INVALID_FILE_SIZE || GetLastError() == NO_ERROR, "Cannot query allocation: " + utf8(p));
    return uint64_t(hi) << 32 | lo;
#else
    struct stat st{};
    require(stat(p.c_str(), &st) == 0, "Cannot query allocation.");
    return uint64_t(st.st_blocks) * 512;
#endif
}
int64_t unix_mtime(const fs::path& p) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d{};
    require(GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d) != 0, "Cannot stat host mirror.");
    uint64_t ticks = uint64_t(d.ftLastWriteTime.dwHighDateTime) << 32 | d.ftLastWriteTime.dwLowDateTime;
    return static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
#else
    struct stat st{};
    require(stat(p.c_str(), &st) == 0, "Cannot stat host mirror.");
    return static_cast<int64_t>(st.st_mtime);
#endif
}
std::string hash_file(File& file, Context& ctx) {
    Bytes b(kIOChunk);
    Sha256 h;
    for (uint64_t off = 0; off < file.size();) {
        ctx.check();
        auto n = size_t(std::min<uint64_t>(b.size(), file.size() - off));
        file.read(off, b.data(), n);
        h.add(b.data(), n);
        off += n;
    }
    return h.finish();
}
std::string read_text(const fs::path& p, size_t limit) {
    File f(p);
    require(f.size() <= limit, "Text file exceeds safety bound: " + utf8(p));
    auto b = f.read(0, size_t(f.size()));
    return std::string(b.begin(), b.end());
}
void write_text_new(const fs::path& p, const std::string& s) {
    File f(p, true, s.size());
    f.write(0, s.data(), s.size());
    f.flush();
}
}

namespace xhc {
void File::verify_zero(uint64_t offset, uint64_t length, Context& ctx) const {
    const uint64_t sz = size();
    require(offset <= sz && length <= sz - offset, "Zero verification exceeds file.");
    uint64_t end = offset + length;
    Bytes b(kIOChunk);
    while (offset < end) {
        ctx.check();
        uint64_t stop = end;
#if !defined(_WIN32) && defined(SEEK_DATA) && defined(SEEK_HOLE)
        errno = 0;
        off_t data = lseek(impl_->fd, static_cast<off_t>(offset), SEEK_DATA);
        if (data < 0 && errno == ENXIO)
            return;
        if (data >= 0) {
            if (uint64_t(data) >= end)
                return;
            offset = uint64_t(data);
            off_t hole = lseek(impl_->fd, data, SEEK_HOLE);
            if (hole >= 0)
                stop = std::min(end, uint64_t(hole));
        } else
            require(errno == EINVAL || errno == ENOTSUP, "Cannot verify sparse output extents.");
#endif
        while (offset < stop) {
            ctx.check();
            size_t n = size_t(std::min<uint64_t>(b.size(), stop - offset));
            read(offset, b.data(), n);
            size_t i = 0;
            for (; i + 8 <= n; i += 8) {
                uint64_t v;
                std::memcpy(&v, b.data() + i, 8);
                if (v != 0)
                    throw Error("Output contains nonzero unallocated/slack data at 0x" + hex64(offset + i));
            }
            for (; i < n; ++i) {
                if (b[i] != 0)
                    throw Error("Output contains nonzero unallocated/slack data.");
            }
            offset += n;
        }
    }
}
}
