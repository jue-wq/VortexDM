#pragma once

#include "vortex/error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace vortex {

// Positional writes allow independent segments to be written concurrently without
// sharing a mutable file cursor. Call flush() before persisting a segment checkpoint.
class CrashSafeFileWriter {
public:
    explicit CrashSafeFileWriter(std::filesystem::path path);
    ~CrashSafeFileWriter();

    CrashSafeFileWriter(const CrashSafeFileWriter&) = delete;
    CrashSafeFileWriter& operator=(const CrashSafeFileWriter&) = delete;
    CrashSafeFileWriter(CrashSafeFileWriter&&) = delete;
    CrashSafeFileWriter& operator=(CrashSafeFileWriter&&) = delete;

    [[nodiscard]] Result<void> open(std::uint64_t expected_size);
    [[nodiscard]] Result<void> write(std::uint64_t offset, std::span<const std::byte> data);
    [[nodiscard]] Result<void> flush();
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

private:
    std::filesystem::path path_;
    std::uint64_t size_{0};
#ifdef _WIN32
    void* native_handle_{nullptr};
#else
    int file_descriptor_{-1};
#endif
};

} // namespace vortex
