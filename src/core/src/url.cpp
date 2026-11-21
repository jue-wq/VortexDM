#include "vortex/url.hpp"
#include <algorithm>
#include <cctype>
namespace vortex {
Result<ParsedUrl> parse_http_url(std::string_view value) {
    if (value.empty() || value.find_first_of("\r\n\t ") != std::string_view::npos)
        return std::unexpected(make_error("invalid_url", "URL is empty or contains whitespace"));
    const auto separator = value.find("://");
    if (separator == std::string_view::npos)
        return std::unexpected(make_error("invalid_url", "URL must include a scheme"));
    std::string scheme(value.substr(0, separator));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (scheme != "http" && scheme != "https")
        return std::unexpected(make_error("unsupported_scheme", "Only HTTP and HTTPS are allowed"));
    auto authority = value.substr(separator + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    if (authority.empty() || authority.front() == ':' || authority.find('@') != std::string_view::npos)
        return std::unexpected(make_error("invalid_host", "URL authority is invalid"));
    return ParsedUrl{scheme, std::string(authority)};
}
} // namespace vortex
