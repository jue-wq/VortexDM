#include "core/network/http_request_policy.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace vortex {
namespace {

[[nodiscard]] bool has_forbidden_url_character(std::string_view value) noexcept {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch <= 0x20U || ch == 0x7fU || ch == '\\';
    });
}

[[nodiscard]] std::string_view scheme_of(std::string_view url) noexcept {
    const auto separator = url.find("://");
    if (separator == std::string_view::npos) {
        return {};
    }
    return url.substr(0, separator);
}

[[nodiscard]] bool equal_ascii_case_insensitive(
    std::string_view left, std::string_view right
) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool HttpRequestPolicy::is_supported_url(std::string_view url) noexcept {
    if (url.empty() || has_forbidden_url_character(url)) {
        return false;
    }

    const auto scheme = scheme_of(url);
    if (!equal_ascii_case_insensitive(scheme, "https") &&
        !equal_ascii_case_insensitive(scheme, "http")) {
        return false;
    }

    const auto authority_start = url.find("://") + 3U;
    const auto authority_end = url.find_first_of("/?#", authority_start);
    const auto authority = url.substr(
        authority_start,
        authority_end == std::string_view::npos
            ? url.size() - authority_start
            : authority_end - authority_start
    );

    // Reject empty hosts and user-info. DNS/IP syntax and port validity are
    // delegated to the HTTP library after this conservative boundary check.
    return !authority.empty() && authority.find('@') == std::string_view::npos;
}

bool HttpRequestPolicy::is_allowed_redirect(
    std::string_view original_url, std::string_view redirect_url
) noexcept {
    if (!is_supported_url(original_url) || !is_supported_url(redirect_url)) {
        return false;
    }

    // HTTPS may redirect to HTTPS only; HTTP is retained for compatibility,
    // but must never be silently upgraded/downgraded by this policy layer.
    const auto original_scheme = scheme_of(original_url);
    const auto redirect_scheme = scheme_of(redirect_url);
    if (equal_ascii_case_insensitive(original_scheme, "https")) {
        return equal_ascii_case_insensitive(redirect_scheme, "https");
    }
    return equal_ascii_case_insensitive(redirect_scheme, "http") ||
           equal_ascii_case_insensitive(redirect_scheme, "https");
}

std::optional<std::string> HttpRequestPolicy::range_header(
    const ByteRange& range
) {
    if (range.last < range.first ||
        range.last == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return "bytes=" + std::to_string(range.first) + "-" +
           std::to_string(range.last);
}

} // namespace vortex
