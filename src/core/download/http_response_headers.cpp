#include "http_response_headers.hpp"

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

bool equal_ascii_ci(const std::string_view left,
                    const std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) return false;
    }
    return true;
}

bool parse_unsigned(const std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool set_consistent(std::optional<std::uint64_t>& target,
                    const std::uint64_t value) noexcept {
    if (target && *target != value) return false;
    target = value;
    return true;
}

bool set_consistent(std::optional<std::string>& target,
                    const std::string_view value) {
    if (target && *target != value) return false;
    target = std::string(value);
    return true;
}

} // namespace

std::optional<HttpResponseHeaders> HttpResponseHeaders::parse(
    const std::span<const std::string_view> lines
) {
    HttpResponseHeaders result{};
    bool saw_status = false;

    for (auto line : lines) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
            line.remove_suffix(1);
        }
        if (line.empty()) continue;

        if (line.starts_with("HTTP/")) {
            if (saw_status) return std::nullopt;
            const auto first_space = line.find(' ');
            if (first_space == std::string_view::npos) return std::nullopt;
            auto status = line.substr(first_space + 1);
            if (status.size() < 3) return std::nullopt;
            int code = 0;
            const auto parsed = std::from_chars(status.data(), status.data() + 3, code);
            if (parsed.ec != std::errc{} || parsed.ptr != status.data() + 3 ||
                code < 100 || code > 599 ||
                (status.size() > 3 && status[3] != ' ' && status[3] != '\t')) {
                return std::nullopt;
            }
            result.status_code = code;
            saw_status = true;
            continue;
        }

        if (!saw_status || line.front() == ' ' || line.front() == '\t') {
            return std::nullopt;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return std::nullopt;
        const auto name = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        trim(value);
        if (value.find_first_of("\r\n") != std::string_view::npos) return std::nullopt;

        if (equal_ascii_ci(name, "content-length")) {
            std::uint64_t length = 0;
            if (!parse_unsigned(value, length) ||
                !set_consistent(result.content_length, length)) return std::nullopt;
        } else if (equal_ascii_ci(name, "content-range")) {
            if (!set_consistent(result.content_range, value)) return std::nullopt;
        } else if (equal_ascii_ci(name, "accept-ranges")) {
            std::size_t start = 0;
            while (start <= value.size()) {
                const auto comma = value.find(',', start);
                auto token = value.substr(start, comma == std::string_view::npos
                    ? value.size() - start : comma - start);
                trim(token);
                if (equal_ascii_ci(token, "bytes")) result.accepts_byte_ranges = true;
                if (comma == std::string_view::npos) break;
                start = comma + 1;
            }
        } else if (equal_ascii_ci(name, "retry-after")) {
            std::uint64_t seconds = 0;
            if (parse_unsigned(value, seconds)) {
                if (seconds > static_cast<std::uint64_t>(
                        std::chrono::seconds::max().count())) return std::nullopt;
                const auto duration = std::chrono::seconds{
                    static_cast<std::chrono::seconds::rep>(seconds)};
                if (result.retry_after && *result.retry_after != duration) return std::nullopt;
                result.retry_after = duration;
            }
        } else if (equal_ascii_ci(name, "etag")) {
            if (!set_consistent(result.etag, value)) return std::nullopt;
        } else if (equal_ascii_ci(name, "last-modified")) {
            if (!set_consistent(result.last_modified, value)) return std::nullopt;
        }
    }

    if (!saw_status) return std::nullopt;
    return result;
}

} // namespace vortex
