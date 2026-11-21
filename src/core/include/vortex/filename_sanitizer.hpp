#pragma once

#include <string>
#include <string_view>

namespace vortex {
[[nodiscard]] std::string sanitize_filename(std::string_view input);
} // namespace vortex
