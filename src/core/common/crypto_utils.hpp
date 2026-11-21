#pragma once

#include "result.hpp"

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <string>
#include <span>

namespace vortex {

class CryptoUtils final {
public:
    [[nodiscard]] static Result<std::string> sha256_file(
        const std::filesystem::path& path
    );
    // Hashes an in-memory byte span using SHA-256.
    [[nodiscard]] static Result<std::string> sha256_bytes(std::span<const std::byte> data);
    // Hashes exactly [offset, offset + length); rejects overflow and truncated ranges.
    [[nodiscard]] static Result<std::string> sha256_file_range(
        const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length
    );
};

} // namespace vortex
