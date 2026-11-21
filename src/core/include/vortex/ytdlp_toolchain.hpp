#pragma once

#include "vortex/error.hpp"

#include <filesystem>
#include <string>

namespace vortex {

[[nodiscard]] Result<std::string> sha256_file(const std::filesystem::path& path);
[[nodiscard]] Result<void> verify_sha256_file(const std::filesystem::path& path, const std::string& expected_hex);

// Stages and verifies a candidate, retains the previous active binary as .rollback,
// and only promotes the candidate after its SHA-256 matches the pinned digest.
[[nodiscard]] Result<void> activate_verified_tool(
    const std::filesystem::path& candidate,
    const std::filesystem::path& active_path,
    const std::string& expected_sha256);

[[nodiscard]] Result<void> rollback_tool(const std::filesystem::path& active_path);

} // namespace vortex
