#include "vortex/application_paths.hpp"

#include <cstdlib>

namespace vortex {

std::filesystem::path application_database_path() {
#ifdef _WIN32
    if (const char* root = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(root) / "VortexDM" / "vortexdm.sqlite3";
#else
    if (const char* root = std::getenv("XDG_DATA_HOME"))
        return std::filesystem::path(root) / "vortexdm" / "vortexdm.sqlite3";
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local" / "share" / "vortexdm" / "vortexdm.sqlite3";
#endif
    return std::filesystem::temp_directory_path() / "vortexdm.sqlite3";
}

std::filesystem::path default_download_directory() {
#ifdef _WIN32
    if (const char* home = std::getenv("USERPROFILE")) return std::filesystem::path(home) / "Downloads";
#else
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / "Downloads";
#endif
    return std::filesystem::current_path() / "Downloads";
}

} // namespace vortex
