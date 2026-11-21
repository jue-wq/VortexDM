#pragma once

#include "core/common/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>

namespace vortex {

// Serializes random-access writes to one pre-sized partial file.
// flush() drains the C++ stream buffer; flush_durable() also requests an OS-level flush.
class OffsetFileWriter final {
public:
    OffsetFileWriter() = default;
    ~OffsetFileWriter();

    OffsetFileWriter(const OffsetFileWriter&) = delete;
    OffsetFileWriter& operator=(const OffsetFileWriter&) = delete;

    [[nodiscard]] Status open(const std::filesystem::path& path, std::uint64_t size);
    // Opens an existing partial file without truncating it; exact size is required.
    [[nodiscard]] Status open_existing(const std::filesystem::path& path, std::uint64_t expected_size);
    [[nodiscard]] Status write_at(std::uint64_t offset, std::span<const std::byte> data);
    [[nodiscard]] Status flush();
    // Flushes stream buffers and requests durable file-data/metadata persistence from the OS.
    [[nodiscard]] Status flush_durable();
    [[nodiscard]] Status close();

private:
    std::fstream file_;
    std::filesystem::path path_;
    std::uint64_t size_{};
    std::mutex mutex_;
};

} // namespace vortex
