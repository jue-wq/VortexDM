#include "http_transport.hpp"

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

struct TransferContext final {
    const HttpBodySink* sink{};
    std::vector<std::string> header_lines;
    const char* callback_error{};
    std::size_t body_bytes{};
    bool sink_failed{};
};

size_t receive_body(char* data, const size_t size, const size_t count, void* opaque) noexcept {
    auto* context = static_cast<TransferContext*>(opaque);
    if (size != 0 && count > static_cast<std::size_t>(-1) / size) return 0;
    const auto bytes = size * count;
    if (bytes == 0) return 0;
    try {
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
        if (line.starts_with("HTTP/")) context->header_lines.clear();
        context->header_lines.emplace_back(std::move(line));
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
        (options.range && (options.range->first > options.range->last ||
                           options.range->last == UINT64_MAX)) ||
        (options.if_range && (options.if_range->empty() ||
            options.if_range->find_first_of("\\r\\n") != std::string::npos)) ||
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
    const std::string url_copy(url);
    const auto set = [&](const CURLoption option, const auto value) {
        return curl_easy_setopt(handle.get(), option, value) == CURLE_OK;
    };

    bool configured =
        set(CURLOPT_URL, url_copy.c_str()) &&
        set(CURLOPT_HTTPGET, 1L) &&\n        (!options.range || set(CURLOPT_RANGE, range_buffer)) &&\n        (!options.if_range || set(CURLOPT_HTTPHEADER, request_headers.get())) &&
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
            context.sink_failed ? ErrorCode::io_error : ErrorCode::network_error,
            "HTTP transfer failed: " + message));
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
