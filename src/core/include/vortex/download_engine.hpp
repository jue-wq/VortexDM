#pragma once
#include "vortex/error.hpp"
#include <filesystem>
#include <string>
namespace vortex {
[[nodiscard]] Result<void> download_file(const std::string& url, const std::filesystem::path& destination);
} // namespace vortex
