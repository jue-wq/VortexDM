#include "vortex/process_runner.hpp"

#include <algorithm>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vortex {
namespace {
void append_bounded(ProcessResult& result, const char* data, std::size_t count, std::size_t limit) {
    const auto available = result.output.size() < limit ? limit - result.output.size() : 0U;
    const auto copied = std::min(available, count);
    result.output.append(data, copied);
    if (copied != count) result.output_truncated = true;
}
#ifdef _WIN32
std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
std::wstring quote_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            result.append(slashes * 2U + 1U, L'\\');
            result.push_back(ch);
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}
#endif
}

Result<ProcessResult> run_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const ProcessOptions& options) {
    if (executable.empty() || !executable.is_absolute()) {
        return std::unexpected(make_error("process_executable", "Executable must be an absolute path"));
    }
    if (options.timeout <= std::chrono::milliseconds::zero() || options.max_output_bytes == 0U) {
        return std::unexpected(make_error("process_options", "Process timeout and output limit must be positive"));
    }
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos) {
            return std::unexpected(make_error("process_argument", "Arguments cannot contain NUL bytes"));
        }
    }
    ProcessResult result;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        return std::unexpected(make_error("process_pipe", "Could not create process output pipe"));
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& argument : arguments) {
        const auto wide = widen(argument);
        if (wide.empty() && !argument.empty()) {
            CloseHandle(read_pipe); CloseHandle(write_pipe);
            return std::unexpected(make_error("process_argument", "Argument is not valid UTF-8"));
        }
        command.push_back(L' ');
        command += quote_argument(wide);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        const auto error = GetLastError();
        CloseHandle(read_pipe); CloseHandle(write_pipe);
        return std::unexpected(make_error("process_start", "CreateProcessW failed: " + std::to_string(error)));
    }
    CloseHandle(write_pipe);
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    bool finished = false;
    while (!finished) {
        DWORD available = 0;
        if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            char buffer[8192];
            DWORD read = 0;
            if (ReadFile(read_pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) && read > 0)
                append_bounded(result, buffer, read, options.max_output_bytes);
        }
        const DWORD wait = WaitForSingleObject(process.hProcess, 20);
        if (wait == WAIT_OBJECT_0) finished = true;
        if (options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed)) {
            result.cancelled = true; TerminateProcess(process.hProcess, 1); finished = true;
        } else if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true; TerminateProcess(process.hProcess, 1); finished = true;
        }
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    result.exit_code = static_cast<int>(code);
    char buffer[8192]; DWORD read = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        append_bounded(result, buffer, read, options.max_output_bytes);
    CloseHandle(read_pipe); CloseHandle(process.hThread); CloseHandle(process.hProcess);
#else
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) return std::unexpected(make_error("process_pipe", "Could not create process output pipe"));
    const pid_t child = ::fork();
    if (child < 0) {
        const int error = errno; ::close(pipe_fds[0]); ::close(pipe_fds[1]);
        return std::unexpected(make_error("process_fork", "fork failed: " + std::to_string(error)));
    }
    if (child == 0) {
        (void)::dup2(pipe_fds[1], STDOUT_FILENO);
        (void)::dup2(pipe_fds[1], STDERR_FILENO);
        ::close(pipe_fds[0]); ::close(pipe_fds[1]);
        std::vector<std::string> storage;
        storage.reserve(arguments.size() + 1U);
        storage.push_back(executable.string());
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1U);
        for (auto& value : storage) argv.push_back(value.data());
        argv.push_back(nullptr);
        ::execv(executable.c_str(), argv.data());
        _exit(127);
    }
    ::close(pipe_fds[1]);
    const int flags = ::fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) (void)::fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    bool finished = false;
    int status = 0;
    while (!finished) {
        pollfd descriptor{pipe_fds[0], POLLIN, 0};
        (void)::poll(&descriptor, 1, 20);
        char buffer[8192];
        for (;;) {
            const auto count = ::read(pipe_fds[0], buffer, sizeof(buffer));
            if (count > 0) append_bounded(result, buffer, static_cast<std::size_t>(count), options.max_output_bytes);
            else break;
        }
        const pid_t waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) finished = true;
        if (options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed)) {
            result.cancelled = true; (void)::kill(child, SIGKILL);
        } else if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true; (void)::kill(child, SIGKILL);
        }
        if (result.cancelled || result.timed_out) {
            (void)::waitpid(child, &status, 0);
            finished = true;
        }
    }
    char buffer[8192];
    for (;;) {
        const auto count = ::read(pipe_fds[0], buffer, sizeof(buffer));
        if (count > 0) append_bounded(result, buffer, static_cast<std::size_t>(count), options.max_output_bytes);
        else break;
    }
    ::close(pipe_fds[0]);
    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
#endif
    if (result.timed_out) return std::unexpected(make_error("process_timeout", "External process exceeded its timeout"));
    if (result.cancelled) return std::unexpected(make_error("process_cancelled", "External process was cancelled"));
    return result;
}

} // namespace vortex
