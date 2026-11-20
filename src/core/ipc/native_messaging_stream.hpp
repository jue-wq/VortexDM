#pragma once
#include "core/common/result.hpp"
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
namespace vortex {
[[nodiscard]] Result<std::optional<std::string>> read_native_message(std::istream& input);
[[nodiscard]] Status write_native_message(std::ostream& output, std::string_view payload);
} // namespace vortex
