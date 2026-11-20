#include "logger.hpp"

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <system_error>

namespace vortex {

Status Logger::initialize(
    const std::filesystem::path& log_file,
    const std::size_t max_file_size,
    const std::size_t max_files
) {
    if (log_file.empty() || max_file_size == 0U || max_files == 0U) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid logger configuration"
        ));
    }

    try {
        const auto parent = log_file.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return std::unexpected(VortexError::make(
                    ErrorCode::io_error, "Unable to create log directory", ec
                ));
            }
        }

        auto logger = spdlog::rotating_logger_mt(
            "vortexdm", log_file.string(), max_file_size, max_files
        );
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
        logger->set_level(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
        return {};
    } catch (const spdlog::spdlog_ex& ex) {
        return std::unexpected(VortexError::make(
            ErrorCode::io_error, ex.what()
        ));
    }
}

std::shared_ptr<spdlog::logger> Logger::get() {
    return spdlog::default_logger();
}

} // namespace vortex
