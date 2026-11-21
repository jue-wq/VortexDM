#include "http_transport.hpp"
#include "core/download/content_range.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <climits>
#include <memory>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace vortex {
namespace {

class CurlGlobal final {
public:
    CurlGlobal() : result_(curl_global_init(CURL_GLOBAL_DEFAULT)) {}
    ~CurlGlobal() { if (result_ == CURLE_OK) curl_global_cleanup(); }
    [[nodiscard]] bool okay() const noexcept { return result_ == CURLE_OK; }
private:
    CURLcode result_;
};

CurlGlobal& curl_global() {
    static CurlGlobal instance;
    return instance;
}

constexpr std::size_t kMaximumResponseHeaderBytes = 256U * 1024U;

struct TransferContext final {
    const HttpBodySink* sink{};
    std::vector<std::string> header_lines;
    const char* callback_error{};
    std::size_t body_bytes{};
    std::size_t header_bytes{};
    bool sink_failed{};
    std::optional<ByteRange> requested_range;
    std::optional<std::uint64_t> expected_total_size;
    bool range_headers_validated{};
    bool range_validation_failed{};
};

bool validate_range_headers(TransferContext& context) {
    if (!context.requested_range) return true;
    std::vector<std::string_view> views;
    views.reserve(context.header_lines.size());
    for (const auto& line : context.header_lines) views.emplace_back(line);
    const auto headers = HttpResponseHeaders::parse(views);
    if (!headers || !headers->content_range || !headers->content_length ||
        !context.expected_total_size ||
        !ContentRangeParser::validate_partial_response(
            headers->status_code, *headers->content_range, *headers->content_length,
            *context.requested_range, *context.expected_total_size)) {
        context.range_validation_failed = true;
        context.callback_error = "HTTP range response did not match the requested byte interval";
        return false;
    }
    context.range_headers_validated = true;
    return true;
}

size_t receive_body(char* data, const size_t size, const size_t count, void* opaque) noexcept {
    auto* context = static_cast<TransferContext*>(opaque);
    if (size != 0 && count > static_cast<std::size_t>(-1) / size) return 0;
    const auto bytes = size * count;
    if (bytes == 0) return 0;
    try {
        if (context->requested_range && !context->range_headers_validated &&
            !validate_range_headers(*context)) return 0;
        const auto view = std::span{
            reinterpret_cast<const std::byte*>(data), bytes
        };
        if (!(*context->sink)(view)) {
            context->sink_failed = true;
            context->callback_error = "HTTP body sink rejected data";
            return 0;
        }
        if (bytes > static_cast<std::size_t>(-1) - context->body_bytes) {
            context->callback_error = "HTTP body byte counter overflow";
            return 0;
        }
        context->body_bytes += bytes;
        return bytes;
    } catch (...) {
        context->sink_failed = true;
        context->callback_error = "HTTP body sink threw an exception";
        return 0;
    }
}

size_t receive_header(char* data, const size_t size, const size_t count,
                      void* opaque) noexcept {
    auto* context = static_cast<TransferContext*>(opaque);
    if (size != 0 && count > static_cast<std::size_t>(-1) / size) return 0;
    const auto bytes = size * count;
    try {
        std::string line(data, bytes);
        if (line.starts_with("HTTP/")) {
            context->header_lines.clear();
            context->header_bytes = 0;
        }
        if (bytes > kMaximumResponseHeaderBytes - context->header_bytes) {
            context->callback_error = "HTTP response headers exceeded the 256 KiB limit";
            return 0;
        }
        context->header_lines.emplace_back(std::move(line));
        context->header_bytes += bytes;
        return bytes;
    } catch (...) {
        context->callback_error = "Unable to collect HTTP response headers";
        return 0;
    }
}

} // namespace

Result<HttpTransferResponse> HttpTransport::get(
    const std::string_view url,
    const HttpBodySink& sink,
    const HttpTransferOptions& options
) const {
    if (url.empty() || !sink || options.connect_timeout_seconds <= 0 ||
        options.total_timeout_seconds < 0 || options.maximum_redirects < 0 ||
        options.user_agent.empty() ||
        (options.range.has_value() != options.expected_total_size.has_value()) ||
        (options.expected_total_size && *options.expected_total_size == 0) ||
        (options.range && options.expected_total_size &&
         options.range->last >= *options.expected_total_size) ||
        (options.range && (options.range->first > options.range->last ||
                           options.range->last == UINT64_MAX)) ||
        (options.if_range && (options.if_range->empty() ||
            options.if_range->find_first_of("\r\n") != std::string::npos)) ||
        (options.if_range && !options.range)) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid HTTP transfer arguments"));
    }
    if (!curl_global().okay()) {
        return std::unexpected(VortexError::make(
            ErrorCode::network_error, "Unable to initialize libcurl"));
    }

    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) {
        return std::unexpected(VortexError::make(
            ErrorCode::network_error, "Unable to create libcurl handle"));
    }

    TransferContext context{};
    context.sink = &sink;
    context.requested_range = options.range;
    context.expected_total_size = options.expected_total_size;
    const std::string url_copy(url);
    const auto set = [&](const CURLoption option, const auto value) {
        return curl_easy_setopt(handle.get(), option, value) == CURLE_OK;
    };

    char range_buffer[64]{};
    if (options.range) {
        const auto written = std::snprintf(range_buffer, sizeof(range_buffer),
            "%llu-%llu",
            static_cast<unsigned long long>(options.range->first),
            static_cast<unsigned long long>(options.range->last));
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(range_buffer)) {
            return std::unexpected(VortexError::make(
                ErrorCode::invalid_argument, "HTTP byte range cannot be represented"));
        }
    }
    using HeaderList = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
    HeaderList request_headers(nullptr, &curl_slist_free_all);
    if (options.if_range) {
        const auto line = "If-Range: " + *options.if_range;
        request_headers.reset(curl_slist_append(nullptr, line.c_str()));
        if (!request_headers) return std::unexpected(VortexError::make(
            ErrorCode::network_error, "Unable to allocate HTTP request headers"));
    }

    bool configured =
        set(CURLOPT_URL, url_copy.c_str()) &&
        set(CURLOPT_HTTPGET, 1L) &&
        (!options.range || set(CURLOPT_RANGE, range_buffer)) &&
        (!options.if_range || set(CURLOPT_HTTPHEADER, request_headers.get())) &&
        set(CURLOPT_FOLLOWLOCATION, 1L) &&
        set(CURLOPT_MAXREDIRS, options.maximum_redirects) &&
        set(CURLOPT_CONNECTTIMEOUT, options.connect_timeout_seconds) &&
        set(CURLOPT_TIMEOUT, options.total_timeout_seconds) &&
        set(CURLOPT_NOSIGNAL, 1L) &&
        set(CURLOPT_SSL_VERIFYPEER, 1L) &&
        set(CURLOPT_SSL_VERIFYHOST, 2L) &&
        set(CURLOPT_PROTOCOLS_STR, "http,https") &&
        set(CURLOPT_REDIR_PROTOCOLS_STR, "https") &&
        set(CURLOPT_USERAGENT, options.user_agent.c_str()) &&
        set(CURLOPT_WRITEFUNCTION, &receive_body) &&
        set(CURLOPT_WRITEDATA, &context) &&
        set(CURLOPT_HEADERFUNCTION, &receive_header) &&
        set(CURLOPT_HEADERDATA, &context);
    if (!configured) {
        return std::unexpected(VortexError::make(
            ErrorCode::network_error, "Unable to configure libcurl options"));
    }

    const auto result = curl_easy_perform(handle.get());
    if (result != CURLE_OK) {
        const auto message = context.callback_error == nullptr
            ? std::string(curl_easy_strerror(result))
            : std::string(context.callback_error);
        return std::unexpected(VortexError::make(
            context.sink_failed ? ErrorCode::io_error :
                (context.range_validation_failed ? ErrorCode::parse_error : ErrorCode::network_error),
            "HTTP transfer failed: " + message));
    }

    if (options.range && !context.range_headers_validated &&
        !validate_range_headers(context)) {
        return std::unexpected(VortexError::make(
            ErrorCode::parse_error, "HTTP range response metadata is invalid"));
    }

    if (options.range && context.body_bytes != options.range->size()) {
        return std::unexpected(VortexError::make(
            ErrorCode::parse_error, "HTTP range body length did not match the requested interval"));
    }

    long status = 0;
    if (curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) {
        return std::unexpected(VortexError::make(
            ErrorCode::network_error, "Unable to read HTTP response status"));
    }

    std::vector<std::string_view> views;
    views.reserve(context.header_lines.size());
    for (const auto& line : context.header_lines) views.emplace_back(line);
    const auto headers = HttpResponseHeaders::parse(views);
    if (!headers || headers->status_code != status) {
        return std::unexpected(VortexError::make(
            ErrorCode::parse_error, "Malformed final HTTP response headers"));
    }

    return HttpTransferResponse{
        status, *headers, context.body_bytes
    };
}

} // namespace vortex
