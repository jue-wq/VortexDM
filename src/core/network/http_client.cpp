#include "core/network/http_client.hpp"

#include "core/network/http_request_policy.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

namespace vortex {
namespace {

struct CurlGlobal final {
    CURLcode result{curl_global_init(CURL_GLOBAL_DEFAULT)};
    ~CurlGlobal() { if (result == CURLE_OK) curl_global_cleanup(); }
};

[[nodiscard]] CURLcode ensure_curl() {
    static CurlGlobal global;
    return global.result;
}

struct TransferContext final {
    const HttpBodySink* sink{};
    std::stop_token stop;
    std::uint64_t bytes{};
    bool sink_failed{};
    std::exception_ptr exception;
    std::map<std::string, std::string, std::less<>> headers;
};

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[nodiscard]] bool is_header_name_character(unsigned char c) noexcept {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z')) return true;
    constexpr std::string_view punctuation{"!#$%&'*+-.^_`|~"};
    return punctuation.find(static_cast<char>(c)) != std::string_view::npos;
}

[[nodiscard]] bool is_valid_header(std::string_view header) noexcept {
    const auto colon = header.find(':');
    if (colon == std::string_view::npos || colon == 0U) return false;
    for (std::size_t i = 0; i < colon; ++i) {
        if (!is_header_name_character(static_cast<unsigned char>(header[i]))) return false;
    }
    for (std::size_t i = colon + 1U; i < header.size(); ++i) {
        const auto c = static_cast<unsigned char>(header[i]);
        // Permit horizontal tab and visible/obs-text bytes; reject CR, LF,
        // NUL, DEL and all other C0 controls before passing to libcurl.
        if ((c < 0x20U && c != '\t') || c == 0x7fU) return false;
    }
    return true;
}

std::size_t write_body(char* data, std::size_t size, std::size_t count,
                       void* opaque) noexcept {
    auto& context = *static_cast<TransferContext*>(opaque);
    if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size) return 0U;
    const auto length = size * count;
    if (context.stop.stop_requested()) return 0U;
    try {
        const auto* begin = reinterpret_cast<const std::byte*>(data);
        if (!(*context.sink)(std::span<const std::byte>{begin, length})) {
            context.sink_failed = true;
            return 0U;
        }
        if (length > std::numeric_limits<std::uint64_t>::max() - context.bytes) return 0U;
        context.bytes += static_cast<std::uint64_t>(length);
        return length;
    } catch (...) {
        context.exception = std::current_exception();
        return 0U;
    }
}

std::size_t read_header(char* data, std::size_t size, std::size_t count,
                        void* opaque) noexcept {
    auto& context = *static_cast<TransferContext*>(opaque);
    if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size) return 0U;
    const auto length = size * count;
    try {
        std::string line(data, length);
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            auto key = lower_ascii(trim(line.substr(0, colon)));
            auto value = trim(line.substr(colon + 1U));
            if (!key.empty()) context.headers[std::move(key)] = std::move(value);
        } else if (line.starts_with("HTTP/")) {
            context.headers.clear();
        }
        return length;
    } catch (...) {
        context.exception = std::current_exception();
        return 0U;
    }
}

int progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    const auto& context = *static_cast<TransferContext*>(opaque);
    return context.stop.stop_requested() ? 1 : 0;
}

struct EasyDeleter { void operator()(CURL* handle) const noexcept { curl_easy_cleanup(handle); } };
struct HeaderDeleter { void operator()(curl_slist* list) const noexcept { curl_slist_free_all(list); } };

} // namespace

Result<HttpResponse> HttpClient::perform(
    const HttpRequest& request, const HttpBodySink& sink, std::stop_token stop_token
) {
    if (!HttpRequestPolicy::is_supported_url(request.url) || !sink ||
        request.connect_timeout.count() <= 0 || request.total_timeout.count() < 0) {
        return std::unexpected(VortexError::make(ErrorCode::invalid_argument,
            "Invalid HTTP request or body sink"));
    }
    if (stop_token.stop_requested()) {
        return std::unexpected(VortexError::make(ErrorCode::cancelled, "HTTP request cancelled"));
    }
    if (ensure_curl() != CURLE_OK) {
        return std::unexpected(VortexError::make(ErrorCode::network_error,
            "Unable to initialize libcurl"));
    }

    std::unique_ptr<CURL, EasyDeleter> handle(curl_easy_init());
    if (!handle) {
        return std::unexpected(VortexError::make(ErrorCode::network_error,
            "Unable to create HTTP request handle"));
    }

    TransferContext context{&sink, stop_token};
    std::unique_ptr<curl_slist, HeaderDeleter> request_headers;
    auto append_header = [&](const std::string& value) -> bool {
        auto* updated = curl_slist_append(request_headers.get(), value.c_str());
        if (!updated) return false;
        (void)request_headers.release();
        request_headers.reset(updated);
        return true;
    };

    for (const auto& value : request.headers) {
        if (!is_valid_header(value) || !append_header(value)) {
            return std::unexpected(VortexError::make(ErrorCode::invalid_argument,
                "Malformed HTTP request header"));
        }
    }
    if (request.range) {
        const auto range = HttpRequestPolicy::range_header(*request.range);
        if (!range || !append_header("Range: " + *range)) {
            return std::unexpected(VortexError::make(ErrorCode::invalid_argument,
                "Invalid byte range or unable to allocate request headers"));
        }
    }

    const auto set = [&](CURLoption option, auto value) {
        return curl_easy_setopt(handle.get(), option, value) == CURLE_OK;
    };
    const auto connect_ms = std::clamp<std::int64_t>(
        request.connect_timeout.count(), 1, std::numeric_limits<long>::max());
    const auto total_ms = std::clamp<std::int64_t>(
        request.total_timeout.count(), 0, std::numeric_limits<long>::max());

    bool configured =
        set(CURLOPT_URL, request.url.c_str()) &&
        set(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS)) &&
        set(CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS)) &&
        set(CURLOPT_FOLLOWLOCATION, 0L) &&
        set(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(connect_ms)) &&
        set(CURLOPT_TIMEOUT_MS, static_cast<long>(total_ms)) &&
        set(CURLOPT_SSL_VERIFYPEER, 1L) &&
        set(CURLOPT_SSL_VERIFYHOST, 2L) &&
        set(CURLOPT_NOSIGNAL, 1L) &&
        set(CURLOPT_WRITEFUNCTION, &write_body) &&
        set(CURLOPT_WRITEDATA, &context) &&
        set(CURLOPT_HEADERFUNCTION, &read_header) &&
        set(CURLOPT_HEADERDATA, &context) &&
        set(CURLOPT_XFERINFOFUNCTION, &progress) &&
        set(CURLOPT_XFERINFODATA, &context) &&
        set(CURLOPT_NOPROGRESS, 0L);
    if (request_headers) configured = configured &&
        set(CURLOPT_HTTPHEADER, request_headers.get());
    if (!configured) {
        return std::unexpected(VortexError::make(ErrorCode::network_error,
            "Unable to configure libcurl request"));
    }

    const auto result = curl_easy_perform(handle.get());
    if (stop_token.stop_requested()) {
        return std::unexpected(VortexError::make(ErrorCode::cancelled, "HTTP request cancelled"));
    }
    if (context.exception) {
        return std::unexpected(VortexError::make(ErrorCode::io_error,
            "HTTP body sink raised an exception"));
    }
    if (context.sink_failed) {
        return std::unexpected(VortexError::make(ErrorCode::io_error,
            "HTTP body sink rejected response data"));
    }
    if (result != CURLE_OK) {
        return std::unexpected(VortexError::make(ErrorCode::network_error,
            curl_easy_strerror(result), std::error_code(static_cast<int>(result),
                std::generic_category())));
    }

    HttpResponse response;
    (void)curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
    char* effective_url = nullptr;
    if (curl_easy_getinfo(handle.get(), CURLINFO_EFFECTIVE_URL, &effective_url) == CURLE_OK &&
        effective_url != nullptr) response.effective_url = effective_url;
    response.headers = std::move(context.headers);
    response.bytes_received = context.bytes;
    return response;
}

} // namespace vortex
