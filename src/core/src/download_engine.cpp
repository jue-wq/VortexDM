#include "vortex/download_engine.hpp"
#include "vortex/url.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <memory>
namespace vortex {
namespace {
size_t write_file(char* data, size_t size, size_t count, void* context) {
    return std::fwrite(data, size, count, static_cast<FILE*>(context));
}
struct CurlGlobal { CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); } ~CurlGlobal() { curl_global_cleanup(); } };
}
Result<void> download_file(const std::string& url, const std::filesystem::path& destination) {
    auto parsed = parse_http_url(url);
    if (!parsed) return std::unexpected(parsed.error());
    static CurlGlobal curl_global; (void)curl_global;
    const auto native = destination.string();
    std::unique_ptr<FILE, decltype(&std::fclose)> output(std::fopen(native.c_str(), "wb"), &std::fclose);
    if (!output) return std::unexpected(make_error("file_open", "Could not open destination file"));
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) return std::unexpected(make_error("curl_init", "Could not initialize libcurl"));
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_file);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, output.get());
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "VortexDM/0.1");
    const auto result = curl_easy_perform(handle.get());
    if (result != CURLE_OK) {
        output.reset(); std::error_code ignored; std::filesystem::remove(destination, ignored);
        return std::unexpected(make_error("network_error", curl_easy_strerror(result)));
    }
    long status = 0; curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status >= 400) {
        output.reset(); std::error_code ignored; std::filesystem::remove(destination, ignored);
        return std::unexpected(make_error("http_error", "Server returned HTTP " + std::to_string(status)));
    }
    return {};
}
} // namespace vortex
