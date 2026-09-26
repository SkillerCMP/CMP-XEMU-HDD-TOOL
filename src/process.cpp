// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/common.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
namespace xhc {
std::wstring quote_windows_argument(const std::wstring& a) {
    std::wstring s = L"\"";
    size_t slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            s.append(slashes * 2 + 1, L'\\');
            s += c;
        } else {
            s.append(slashes, L'\\');
            s += c;
        }
        slashes = 0;
    }
    s.append(slashes * 2, L'\\');
    return s + L'"';
}
#ifdef _WIN32
static std::wstring widen(const std::string& s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    require(n > 0, "Invalid UTF-8 process argument.");
    std::wstring w(size_t(n), L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), n) == n,
            "Cannot encode process argument.");
    return w;
}
struct Handle {
    HANDLE h = nullptr;
    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
#endif
ProcessResult run_process(const fs::path& exe, const std::vector<std::string>& args, Context& ctx) {
    ctx.check();
    ProcessResult result;
    auto add = [&](const char* p, size_t n) {
        require(result.output.size() + n <= 8 * 1024 * 1024, "Helper output exceeds safety limit.");
        result.output.append(p, n);
    };
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle rd, wr;
    require(CreatePipe(&rd.h, &wr.h, &sa, 0) != 0, "Cannot create helper output pipe.");
    require(SetHandleInformation(rd.h, HANDLE_FLAG_INHERIT, 0) != 0, "Cannot isolate pipe handle.");
    Handle input;
    input.h = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    require(input.h != INVALID_HANDLE_VALUE, "Cannot open helper input.");
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = input.h;
    si.hStdOutput = wr.h;
    si.hStdError = wr.h;
    std::wstring cmd = quote_windows_argument(exe.wstring());
    for (const auto& a : args)
        cmd += L" " + quote_windows_argument(widen(a));
    Handle job;
    job.h = CreateJobObjectW(nullptr, nullptr);
    require(job.h != nullptr, "Cannot create helper job.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &lim, sizeof(lim)) != 0,
            "Cannot protect helper lifetime.");
    PROCESS_INFORMATION pi{};
    const BOOL launched = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                         CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    const DWORD launch_error = GetLastError();
    require(launched != 0, "Cannot launch helper: " + utf8(exe) + " (Win32 " + std::to_string(launch_error) + ")");
    Handle process, thread;
    process.h = pi.hProcess;
    thread.h = pi.hThread;
    if (!AssignProcessToJobObject(job.h, process.h)) {
        TerminateProcess(process.h, 1);
        WaitForSingleObject(process.h, INFINITE);
        throw Error("Cannot attach helper lifetime guard.");
    }
    require(ResumeThread(thread.h) != DWORD(-1), "Cannot resume helper.");
    CloseHandle(wr.h);
    wr.h = nullptr;
    try {
        bool pipe_closed = false;
        for (;;) {
            ctx.check();
            DWORD available = 0;
            if (!pipe_closed && !PeekNamedPipe(rd.h, nullptr, 0, nullptr, &available, nullptr)) {
                require(GetLastError() == ERROR_BROKEN_PIPE, "Cannot inspect helper output.");
                pipe_closed = true;
            }
            if (available) {
                char buffer[8192];
                DWORD count = 0;
                require(ReadFile(rd.h, buffer, std::min<DWORD>(available, sizeof(buffer)), &count, nullptr) != 0,
                        "Cannot read helper output.");
                add(buffer, count);
                continue;
            }
            DWORD wait = WaitForSingleObject(process.h, 40);
            require(wait != WAIT_FAILED, "Cannot wait for helper process.");
            if (wait == WAIT_OBJECT_0) {
                // Drain any bytes written just before process exit.
                if (!pipe_closed && PeekNamedPipe(rd.h, nullptr, 0, nullptr, &available, nullptr) && available)
                    continue;
                break;
            }
        }
        DWORD code = 0;
        require(GetExitCodeProcess(process.h, &code) != 0, "Cannot read helper exit code.");
        result.code = static_cast<int>(code);
    } catch (...) {
        TerminateJobObject(job.h, 1);
        WaitForSingleObject(process.h, INFINITE);
        throw;
    }
#else
    int pipefd[2];
    require(pipe2(pipefd, O_CLOEXEC) == 0, "Cannot create helper pipe.");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 2);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    std::vector<std::string> text{exe.string()};
    text.insert(text.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : text)
        argv.push_back(s.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, exe.c_str(), &fa, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    close(pipefd[1]);
    if (rc) {
        close(pipefd[0]);
        throw Error("Cannot launch helper: " + utf8(exe) + " (errno " + std::to_string(rc) + ")");
    }
    fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
    bool reaped = false;
    int status = 0;
    try {
        for (;;) {
            ctx.check();
            char buffer[8192];
            ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
            if (n > 0) {
                add(buffer, size_t(n));
                continue;
            }
            if (n < 0 && errno != EAGAIN && errno != EINTR)
                throw Error("Cannot read helper output.");
            if (!reaped) {
                pid_t r = waitpid(pid, &status, WNOHANG);
                if (r == pid)
                    reaped = true;
                else if (r < 0 && errno != EINTR)
                    throw Error("Cannot wait for helper.");
            }
            if (reaped && n == 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        close(pipefd[0]);
        result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    } catch (...) {
        kill(-pid, SIGKILL);
        if (!reaped)
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        close(pipefd[0]);
        throw;
    }
#endif
    return result;
}
fs::path executable_directory() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    require(n > 0 && n < buf.size(), "Cannot locate executable.");
    buf.resize(n);
    return fs::path(buf).parent_path();
#else
    std::vector<char> buf(32768);
    ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size());
    require(n > 0 && size_t(n) < buf.size(), "Cannot locate executable.");
    return fs::path(std::string(buf.data(), size_t(n))).parent_path();
#endif
}
fs::path find_qemu(const fs::path& requested) {
    if (!requested.empty())
        return absolute_safe(requested, true);
#ifdef _WIN32
    const char* name = "qemu-img.exe";
    const char separator = ';';
#else
    const char* name = "qemu-img";
    const char separator = ':';
#endif
    std::vector<fs::path> paths{executable_directory() / "tools" / name, executable_directory() / name};
    if (const char* env = std::getenv("PATH")) {
        std::string p(env);
        size_t start = 0;
        for (;;) {
            size_t end = p.find(separator, start);
            auto part = p.substr(start, end == std::string::npos ? end : end - start);
            if (!part.empty())
                paths.push_back(fs::u8path(part) / name);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return absolute_safe(p, true);
    }
    throw Error(
        "qemu-img helper not found. Select a trusted qemu-img executable or place it with its matching runtime files in the tools folder beside this program. Raw image -> Folder-HDD does not need it.");
}
}
