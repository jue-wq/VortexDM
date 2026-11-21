#include "vortex/crash_safe_file_writer.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <system_error>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace vortex {
namespace {
Error system_error(std::string code, const std::string& action, int error_value) {
#ifdef _WIN32
    return make_error(std::move(code), action + ": " + std::system_category().message(error_value));
#else
    return make_error(std::move(code), action + ": " + std::generic_category().message(error_value));
#endif
}
}

CrashSafeFileWriter::CrashSafeFileWriter(std::filesystem::path path)
    : path_(std::move(path)) {}

CrashSafeFileWriter::~CrashSafeFileWriter() { close(); }

bool CrashSafeFileWriter::is_open() const noexcept {
#ifdef _WIN32
    return native_handle_ != nullptr;
#else
    return file_descriptor_ >= 0;
#endif
}

Result<void> CrashSafeFileWriter::open(std::uint64_t expected_size) {
    if (is_open()) return std::unexpected(make_error("file_already_open", "The output file is already open"));
#ifdef _WIN32
    if (expected_size > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
        return std::unexpected(make_error("file_size", "Requested file size exceeds the Windows file limit"));
    }
    HANDLE handle = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(system_error("file_open", "Could not open output file", static_cast<int>(GetLastError())));
    }
    LARGE_INTEGER end_position{};
    end_position.QuadPart = static_cast<LONGLONG>(expected_size);
    if (!SetFilePointerEx(handle, end_position, nullptr, FILE_BEGIN) || !SetEndOfFile(handle)) {
        const int error_value = static_cast<int>(GetLastError());
        CloseHandle(handle);
        return std::unexpected(system_error("file_preallocate", "Could not size output file", error_value));
    }
    native_handle_ = handle;
#else
    if (expected_size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return std::unexpected(make_error("file_size", "Requested file size exceeds the POSIX file limit"));
    }
    const int descriptor = ::open(path_.c_str(), O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        return std::unexpected(system_error("file_open", "Could not open output file", errno));
    }
    if (::ftruncate(descriptor, static_cast<off_t>(expected_size)) != 0) {
        const int error_value = errno;
        (void)::close(descriptor);
        return std::unexpected(system_error("file_preallocate", "Could not size output file", error_value));
    }
    file_descriptor_ = descriptor;
#endif
    size_ = expected_size;
    return {};
}

Result<void> CrashSafeFileWriter::open_existing(std::uint64_t expected_size) {
    if (is_open()) return std::unexpected(make_error("file_already_open", "The output file is already open"));
#ifdef _WIN32
    if (expected_size > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
        return std::unexpected(make_error("file_size", "Requested file size exceeds the Windows file limit"));
    }
    HANDLE handle = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(system_error("file_open", "Could not reopen partial output file", static_cast<int>(GetLastError())));
    }
    LARGE_INTEGER actual_size{};
    if (!GetFileSizeEx(handle, &actual_size) || actual_size.QuadPart < 0 ||
        static_cast<std::uint64_t>(actual_size.QuadPart) != expected_size) {
        const int error_value = static_cast<int>(GetLastError());
        CloseHandle(handle);
        return std::unexpected(make_error("file_size_mismatch", "Existing partial file size does not match its recovery journal (system error " + std::to_string(error_value) + ")"));
    }
    native_handle_ = handle;
#else
    if (expected_size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return std::unexpected(make_error("file_size", "Requested file size exceeds the POSIX file limit"));
    }
    const int descriptor = ::open(path_.c_str(), O_RDWR | O_CLOEXEC);
    if (descriptor < 0) {
        return std::unexpected(system_error("file_open", "Could not reopen partial output file", errno));
    }
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) != 0) {
        const int error_value = errno;
        (void)::close(descriptor);
        return std::unexpected(system_error("file_stat", "Could not inspect partial output file", error_value));
    }
    if (metadata.st_size < 0 || static_cast<std::uint64_t>(metadata.st_size) != expected_size) {
        (void)::close(descriptor);
        return std::unexpected(make_error("file_size_mismatch", "Existing partial file size does not match its recovery journal"));
    }
    file_descriptor_ = descriptor;
#endif
    size_ = expected_size;
    return {};
}

Result<void> CrashSafeFileWriter::write(std::uint64_t offset, std::span<const std::byte> data) {
    if (!is_open()) return std::unexpected(make_error("file_not_open", "The output file is not open"));
    if (offset > size_ || data.size() > size_ - offset) {
        return std::unexpected(make_error("write_range", "The write exceeds the preallocated file range"));
    }
    if (data.empty()) return {};

#ifdef _WIN32
    HANDLE handle = static_cast<HANDLE>(native_handle_);
    std::size_t completed = 0;
    while (completed < data.size()) {
        const std::size_t remaining = data.size() - completed;
        const DWORD chunk_size = static_cast<DWORD>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        const std::uint64_t current_offset = offset + static_cast<std::uint64_t>(completed);
        HANDLE event_handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event_handle == nullptr) {
            return std::unexpected(system_error("file_write", "Could not create write event", static_cast<int>(GetLastError())));
        }
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(current_offset & 0xffffffffULL);
        overlapped.OffsetHigh = static_cast<DWORD>(current_offset >> 32U);
        overlapped.hEvent = event_handle;
        DWORD written = 0;
        BOOL succeeded = WriteFile(handle, data.data() + completed, chunk_size, &written, &overlapped);
        if (!succeeded && GetLastError() == ERROR_IO_PENDING) {
            succeeded = GetOverlappedResult(handle, &overlapped, &written, TRUE);
        }
        const int error_value = succeeded ? 0 : static_cast<int>(GetLastError());
        CloseHandle(event_handle);
        if (!succeeded) {
            return std::unexpected(system_error("file_write", "Could not write output segment", error_value));
        }
        if (written == 0) {
            return std::unexpected(make_error("file_write", "Operating system completed a zero-byte write"));
        }
        completed += static_cast<std::size_t>(written);
    }
#else
    std::size_t completed = 0;
    while (completed < data.size()) {
        const std::uint64_t current_offset = offset + static_cast<std::uint64_t>(completed);
        const std::size_t remaining = data.size() - completed;
        const std::size_t chunk_size = std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const ssize_t written = ::pwrite(file_descriptor_, data.data() + completed, chunk_size,
                                         static_cast<off_t>(current_offset));
        if (written < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(system_error("file_write", "Could not write output segment", errno));
        }
        if (written == 0) {
            return std::unexpected(make_error("file_write", "Operating system completed a zero-byte write"));
        }
        completed += static_cast<std::size_t>(written);
    }
#endif
    return {};
}

Result<void> CrashSafeFileWriter::flush() {
    if (!is_open()) return std::unexpected(make_error("file_not_open", "The output file is not open"));
#ifdef _WIN32
    if (!FlushFileBuffers(static_cast<HANDLE>(native_handle_))) {
        return std::unexpected(system_error("file_sync", "Could not flush output file", static_cast<int>(GetLastError())));
    }
#else
    if (::fsync(file_descriptor_) != 0) {
        return std::unexpected(system_error("file_sync", "Could not flush output file", errno));
    }
#endif
    return {};
}

void CrashSafeFileWriter::close() noexcept {
#ifdef _WIN32
    if (native_handle_ != nullptr) {
        (void)FlushFileBuffers(static_cast<HANDLE>(native_handle_));
        CloseHandle(static_cast<HANDLE>(native_handle_));
        native_handle_ = nullptr;
    }
#else
    if (file_descriptor_ >= 0) {
        (void)::fsync(file_descriptor_);
        (void)::close(file_descriptor_);
        file_descriptor_ = -1;
    }
#endif
    size_ = 0;
}

} // namespace vortex
