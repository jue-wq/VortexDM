#pragma once

#include "result.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace spdlog {
class logger;
}

namespace vortex {

class Logger final {
public:
    [[nodiscard]] static Status initialize(
        const std::filesystem::path& log_file,
        std::size_t max_file_size = 5U * 1024U * 1024U,
        std::size_t max_files = 3U
    );

    [[nodiscard]] static std::shared_ptr<spdlog::logger> get();
};

} // namespace vortex
