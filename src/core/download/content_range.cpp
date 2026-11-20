#include "content_range.hpp"

#include <charconv>
#include <cctype>
#include <string_view>
#include <system_error>

namespace vortex {
namespace {

void trim(std::string_view& value) noexcept {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
}

bool parse_number(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

} // namespace

std::optional<ContentRange> ContentRangeParser::parse(
    std::string_view header
) noexcept {
    trim(header);
    constexpr std::string_view unit = "bytes";
    if (header.size() < unit.size()) return std::nullopt;
    for (std::size_t i = 0; i < unit.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(header[i])) != unit[i]) {
            return std::nullopt;
        }
    }
    header.remove_prefix(unit.size());
    if (header.empty() ||
        std::isspace(static_cast<unsigned char>(header.front())) == 0) {
        return std::nullopt;
    }
    trim(header);

    const auto dash = header.find('-');
    const auto slash = header.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos ||
        dash == 0 || slash <= dash + 1 || slash + 1 >= header.size() ||
        header.find('-', dash + 1) != std::string_view::npos ||
        header.find('/', slash + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    ContentRange parsed{};
    if (!parse_number(header.substr(0, dash), parsed.range.first) ||
        !parse_number(header.substr(dash + 1, slash - dash - 1), parsed.range.last) ||
        !parse_number(header.substr(slash + 1), parsed.total_size) ||
        parsed.total_size == 0 || parsed.range.first > parsed.range.last ||
        parsed.range.last >= parsed.total_size) {
        return std::nullopt;
    }
    return parsed;
}

} // namespace vortex
