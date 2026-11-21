#pragma once

#include <filesystem>

namespace vortex {

[[nodiscard]] std::filesystem::path application_database_path();
[[nodiscard]] std::filesystem::path default_download_directory();

} // namespace vortex
