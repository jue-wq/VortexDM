#pragma once

#include <string>
#include <system_error>
#include <utility>

namespace vortex {

enum class ErrorCode {
    invalid_argument,
    io_error,
    database_error,
    network_error,
    process_error,
    parse_error,
    cancelled,
    internal_error
};

struct VortexError {
    ErrorCode code{ErrorCode::internal_error};
    std::string message;
    std::error_code system_error{};

    [[nodiscard]] static VortexError make(
        const ErrorCode code,
        std::string message,
        const std::error_code system_error = {}
    ) {
        return VortexError{code, std::move(message), system_error};
    }
};

} // namespace vortex
