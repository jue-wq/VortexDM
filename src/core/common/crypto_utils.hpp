#pragma once

#include "result.hpp"

#include <filesystem>
#include <string>

namespace vortex {

class CryptoUtils final {
public:
    [[nodiscard]] static Result<std::string> sha256_file(
        const std::filesystem::path& path
    );
};

} // namespace vortex
