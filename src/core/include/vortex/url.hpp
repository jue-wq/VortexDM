#pragma once
#include "vortex/error.hpp"
#include <string>
#include <string_view>
namespace vortex {
struct ParsedUrl { std::string scheme; std::string host; };
[[nodiscard]] Result<ParsedUrl> parse_http_url(std::string_view value);
} // namespace vortex
