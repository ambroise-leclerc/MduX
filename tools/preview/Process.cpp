/** @file Process.cpp
 * @brief POSIX and Windows implementations of mdux-preview's bounded child process runner.
 *
 * Compiled as C++17 beside the HTTP transport: operating-system headers stay out of the C++23 module
 * graph for the same reason cpp-httplib's do (ADR-024). The standard library does not start processes,
 * and `std::system` would hand every argument to a shell, so each platform's primitive is used directly.
 */

// clang-format off
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>
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
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#include "Process.hpp"
// clang-format on

namespace mdux::tools::preview {
namespace {
using Clock = std::chrono::steady_clock;

void keep(std::string& sink, const char* data, std::size_t size, std::size_t limit) {
    if (sink.size() < limit)
        sink.append(data, std::min(size, limit - sink.size()));
}

#ifdef _WIN32
std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

// CommandLineToArgvW's rules: backslashes are literal unless they precede a quote.
void quote(std::wstring& line, const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += argument;
        return;
    }
    line += L'"';
    for (std::size_t i = 0;; ++i) {
        std::size_t slashes = 0;
        while (i < argument.size() && argument[i] == L'\\') {
            ++i;
            ++slashes;
        }
        if (i == argument.size()) {
            line.append(slashes * 2, L'\\');
            break;
        }
        if (argument[i] == L'"')
            line.append(slashes * 2 + 1, L'\\');
        else
            line.append(slashes, L'\\');
        line += argument[i];
    }
    line += L'"';
}

std::wstring environmentBlock(const std::vector<std::pair<std::string, std::string>>& overrides) {
    std::vector<std::wstring> entries;
    const auto                upper = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
            return static_cast<wchar_t>(c >= L'a' && c <= L'z' ? c - L'a' + L'A' : c);
        });
        return s;
    };
    const auto overridden = [&](const std::wstring& entry) {
        const auto key = upper(entry.substr(0, entry.find(L'=', 1)));
        return std::any_of(overrides.begin(), overrides.end(), [&](const auto& o) {
            return upper(wide(o.first)) == key;
        });
    };
    if (auto* strings = GetEnvironmentStringsW()) {
        for (auto* p = strings; *p != L'\0'; p += wcslen(p) + 1) {
            std::wstring entry(p);
            if (!overridden(entry))
                entries.push_back(std::move(entry));
        }
        FreeEnvironmentStringsW(strings);
    }
    for (const auto& [key, value] : overrides)
        entries.push_back(wide(key) + L"=" + wide(value));
    std::sort(entries.begin(), entries.end(), [&](const std::wstring& a, const std::wstring& b) {
        return upper(a) < upper(b);
    });
    std::wstring block;
    for (const auto& entry : entries) {
        block += entry;
        block += L'\0';
    }
    block += L'\0';
    return block;
}

struct Handle {
    HANDLE value{nullptr};
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&)            = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        reset();
    }
    void reset() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
        value = nullptr;
    }
};

bool pipe(Handle& read, Handle& write, bool childReads) {
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE              r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, &attributes, 0))
        return false;
    read.value  = r;
    write.value = w;
    // Only the child's end is inheritable; the handle list below restricts inheritance further.
    return SetHandleInformation(childReads ? write.value : read.value, HANDLE_FLAG_INHERIT, 0) != 0;
}

void drain(HANDLE from, std::string& sink, std::size_t limit) {
    char  buffer[4096];
    DWORD count = 0;
    while (ReadFile(from, buffer, static_cast<DWORD>(sizeof buffer), &count, nullptr) && count > 0)
        keep(sink, buffer, count, limit);
}
#endif
}  // namespace

#ifdef _WIN32
ProcessResult runProcess(const ProcessRequest& request) {
    ProcessResult result;
    if (request.arguments.empty())
        return result;
    Handle inRead, inWrite, outRead, outWrite, errRead, errWrite;
    if (!pipe(inRead, inWrite, true) || !pipe(outRead, outWrite, false) || !pipe(errRead, errWrite, false))
        return result;
    std::wstring line;
    for (std::size_t i = 0; i < request.arguments.size(); ++i) {
        if (i != 0)
            line += L' ';
        quote(line, wide(request.arguments[i]));
    }
    auto   block = environmentBlock(request.environment);
    HANDLE inherited[3]{inRead.value, outWrite.value, errWrite.value};
    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
    std::vector<BYTE> storage(listSize);
    STARTUPINFOEXW    startup{};
    startup.StartupInfo.cb         = sizeof startup;
    startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput  = inRead.value;
    startup.StartupInfo.hStdOutput = outWrite.value;
    startup.StartupInfo.hStdError  = errWrite.value;
    startup.lpAttributeList        = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &listSize))
        return result;
    const bool listed = UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, nullptr, nullptr)
                        != 0;
    Handle                               job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    PROCESS_INFORMATION process{};
    const bool created = listed && job.value != nullptr && SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof limits)
                         && CreateProcessW(nullptr,
                                           line.data(),
                                           nullptr,
                                           nullptr,
                                           TRUE,
                                           CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                                           block.data(),
                                           nullptr,
                                           &startup.StartupInfo,
                                           &process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created)
        return result;
    Handle processHandle(process.hProcess), threadHandle(process.hThread);
    if (!AssignProcessToJobObject(job.value, processHandle.value)) {
        TerminateProcess(processHandle.value, 1);
        return result;
    }
    ResumeThread(threadHandle.value);
    result.started = true;
    inRead.reset();
    outWrite.reset();
    errWrite.reset();
    std::thread writer([&] {
        DWORD written = 0;
        for (std::size_t offset = 0; offset < request.input.size(); offset += written)
            if (!WriteFile(inWrite.value,
                           request.input.data() + offset,
                           static_cast<DWORD>(std::min<std::size_t>(request.input.size() - offset, 65536)),
                           &written,
                           nullptr))
                break;
        inWrite.reset();
    });
    std::thread out([&] {
        drain(outRead.value, result.output, request.outputLimit);
    });
    std::thread err([&] {
        drain(errRead.value, result.errors, request.outputLimit);
    });
    const auto  milliseconds = static_cast<DWORD>(std::min<long long>(request.timeout.count(), 0x7fffffffLL));
    if (WaitForSingleObject(processHandle.value, milliseconds) != WAIT_OBJECT_0) {
        result.timedOut = true;
        TerminateJobObject(job.value, 1);
        WaitForSingleObject(processHandle.value, INFINITE);
    }
    DWORD code = 1;
    GetExitCodeProcess(processHandle.value, &code);
    result.exitCode = static_cast<int>(code);
    // Grandchildren still holding a pipe die with the job, which ends the readers.
    TerminateJobObject(job.value, 1);
    writer.join();
    out.join();
    err.join();
    return result;
}
#else
ProcessResult runProcess(const ProcessRequest& request) {
    ProcessResult result;
    if (request.arguments.empty())
        return result;
    int  in[2]{-1, -1}, out[2]{-1, -1}, err[2]{-1, -1};
    auto closeFd = [](int& fd) {
        if (fd >= 0)
            close(fd);
        fd = -1;
    };
    auto closeAll = [&] {
        for (int* fd : {&in[0], &in[1], &out[0], &out[1], &err[0], &err[1]})
            closeFd(*fd);
    };
    for (int* p : {in, out, err}) {
        if (::pipe(p) != 0) {
            closeAll();
            return result;
        }
        for (int i = 0; i < 2; ++i)
            fcntl(p[i], F_SETFD, FD_CLOEXEC);
    }
    std::vector<std::string> variables;
    for (char** e = environ; *e != nullptr; ++e) {
        std::string entry(*e);
        const auto  key = entry.substr(0, entry.find('='));
        if (std::none_of(request.environment.begin(), request.environment.end(), [&](const auto& o) {
                return o.first == key;
            }))
            variables.push_back(std::move(entry));
    }
    for (const auto& [key, value] : request.environment)
        variables.push_back(key + "=" + value);
    std::vector<char*> envp, argv;
    for (auto& v : variables)
        envp.push_back(v.data());
    envp.push_back(nullptr);
    std::vector<std::string> arguments = request.arguments;
    for (auto& a : arguments)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t          attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err[1], 2);
    // The service ignores SIGPIPE; a child must not inherit that, and gets its own process group so a
    // timeout can kill anything it started.
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP);
    pid_t      pid     = 0;
    const bool spawned = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), envp.data()) == 0;
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    closeFd(in[0]);
    closeFd(out[1]);
    closeFd(err[1]);
    if (!spawned) {
        closeAll();
        return result;
    }
    result.started = true;
    for (int fd : {in[1], out[0], err[0]})
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    if (request.input.empty())
        closeFd(in[1]);

    const auto  deadline = Clock::now() + request.timeout;
    std::size_t written  = 0;
    char        buffer[4096];
    while (out[0] >= 0 || err[0] >= 0) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) {
            result.timedOut = true;
            break;
        }
        std::vector<pollfd> watched;
        for (int fd : {out[0], err[0]})
            if (fd >= 0)
                watched.push_back({fd, POLLIN, 0});
        if (in[1] >= 0)
            watched.push_back({in[1], POLLOUT, 0});
        if (poll(watched.data(), static_cast<nfds_t>(watched.size()), static_cast<int>(std::min<long long>(left, 1000))) < 0 && errno != EINTR)
            break;
        for (const auto& w : watched) {
            if (w.revents == 0)
                continue;
            if (w.fd == in[1]) {
                const auto n = write(in[1], request.input.data() + written, request.input.size() - written);
                if (n > 0)
                    written += static_cast<std::size_t>(n);
                if (n < 0 && errno != EAGAIN && errno != EINTR)
                    closeFd(in[1]);
                else if (written == request.input.size())
                    closeFd(in[1]);
                continue;
            }
            const bool isOut = w.fd == out[0];
            const auto n     = read(w.fd, buffer, sizeof buffer);
            if (n > 0)
                keep(isOut ? result.output : result.errors, buffer, static_cast<std::size_t>(n), request.outputLimit);
            else if (n == 0 || (errno != EAGAIN && errno != EINTR))
                closeFd(isOut ? out[0] : err[0]);
        }
    }
    int status = 0;
    for (;;) {
        const auto done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0 && errno != EINTR)
            break;
        if (result.timedOut || Clock::now() >= deadline) {
            result.timedOut = true;
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (result.timedOut)
        kill(-pid, SIGKILL);
    closeAll();
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}
#endif
}  // namespace mdux::tools::preview
