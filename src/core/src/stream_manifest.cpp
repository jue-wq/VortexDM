#include "vortex/stream_manifest.hpp"

#include "vortex/url.hpp"

#include <curl/curl.h>

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace vortex {
namespace {
std::string trim_line(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    const auto first = value.find_first_not_of(" \t");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

std::string attribute(const std::string& line, const std::string& name) {
    const std::string needle = name + "=";
    auto position = line.find(needle);
    if (position == std::string::npos) return {};
    position += needle.size();
    if (position < line.size() && line[position] == '"') {
        const auto end = line.find('"', position + 1U);
        return end == std::string::npos ? std::string{} : line.substr(position + 1U, end - position - 1U);
    }
    const auto end = line.find(',', position);
    return line.substr(position, end == std::string::npos ? std::string::npos : end - position);
}

Result<std::string> resolve_uri(const std::string& base, const std::string& reference) {
    if (reference.empty()) return std::unexpected(make_error("manifest_uri", "Manifest URI reference is empty"));
    auto* handle = curl_url();
    if (!handle) return std::unexpected(make_error("manifest_uri", "Could not allocate URL resolver"));
    CURLUcode code = curl_url_set(handle, CURLUPART_URL, base.c_str(), 0);
    if (code == CURLUE_OK) code = curl_url_set(handle, CURLUPART_URL, reference.c_str(), 0);
    char* resolved = nullptr;
    if (code == CURLUE_OK) code = curl_url_get(handle, CURLUPART_URL, &resolved, 0);
    std::string result;
    if (code == CURLUE_OK && resolved) result = resolved;
    if (resolved) curl_free(resolved);
    curl_url_cleanup(handle);
    if (code != CURLUE_OK) return std::unexpected(make_error("manifest_uri", "Could not resolve manifest URI reference"));
    auto parsed = parse_http_url(result);
    if (!parsed) return std::unexpected(parsed.error());
    return result;
}

std::uint64_t unsigned_attribute(const std::string& line, const std::string& key) {
    const auto value = attribute(line, key);
    std::uint64_t result = 0;
    if (!value.empty()) (void)std::from_chars(value.data(), value.data() + value.size(), result);
    return result;
}
}

Result<HlsManifest> parse_hls_playlist(const std::string& text, const std::string& playlist_url) {
    auto base = parse_http_url(playlist_url);
    if (!base) return std::unexpected(base.error());
    if (text.size() > 8U * 1024U * 1024U) return std::unexpected(make_error("manifest_size", "HLS playlist exceeds 8 MiB"));
    if (text.find("#EXTM3U") == std::string::npos) return std::unexpected(make_error("hls_header", "HLS playlist must start with #EXTM3U"));
    HlsManifest manifest;
    bool awaiting_variant = false;
    bool awaiting_segment = false;
    std::uint64_t pending_bandwidth = 0;
    std::string pending_codecs;
    std::string pending_resolution;
    double pending_duration = 0.0;
    std::string pending_title;
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const auto end = text.find('\n', offset);
        auto line = trim_line(text.substr(offset, end == std::string::npos ? std::string::npos : end - offset));
        offset = end == std::string::npos ? text.size() + 1U : end + 1U;
        if (line.empty()) continue;
        if (line == "#EXT-X-ENDLIST") { manifest.live = false; continue; }
        if (line.starts_with("#EXT-X-STREAM-INF:")) {
            manifest.master_playlist = true;
            awaiting_variant = true;
            pending_bandwidth = unsigned_attribute(line, "BANDWIDTH");
            pending_codecs = attribute(line, "CODECS");
            pending_resolution = attribute(line, "RESOLUTION");
            continue;
        }
        if (line.starts_with("#EXTINF:")) {
            const auto value = line.substr(8U);
            const auto comma = value.find(',');
            const auto duration = value.substr(0U, comma);
            char* end_pointer = nullptr;
            pending_duration = std::strtod(duration.c_str(), &end_pointer);
            if (end_pointer == duration.c_str() || pending_duration < 0.0) pending_duration = 0.0;
            pending_title = comma == std::string::npos ? std::string{} : value.substr(comma + 1U);
            awaiting_segment = true;
            continue;
        }
        if (line.starts_with("#EXT-X-KEY:")) {
            const auto method = attribute(line, "METHOD");
            if (method == "AES-128") {
                const auto key_uri = resolve_uri(playlist_url, attribute(line, "URI"));
                if (!key_uri) return std::unexpected(key_uri.error());
                manifest.aes128_key_uri = *key_uri;
            } else if (method != "NONE" && !method.empty()) {
                return std::unexpected(make_error("hls_encryption_unsupported", "Only HLS AES-128 and unencrypted playlists are supported; SAMPLE-AES/DRM is not handled"));
            }
            continue;
        }
        if (line.starts_with("#EXT-X-MAP:")) {
            const auto uri = resolve_uri(playlist_url, attribute(line, "URI"));
            if (!uri) return std::unexpected(uri.error());
            manifest.initialization_uri = *uri;
            continue;
        }
        if (line.front() == '#') continue;
        const auto resolved = resolve_uri(playlist_url, line);
        if (!resolved) return std::unexpected(resolved.error());
        if (awaiting_variant) {
            manifest.variants.push_back({*resolved, pending_bandwidth, pending_codecs, pending_resolution});
            awaiting_variant = false;
        } else if (awaiting_segment) {
            manifest.segments.push_back({*resolved, pending_duration, pending_title});
            awaiting_segment = false;
            pending_duration = 0.0;
            pending_title.clear();
        }
    }
    if (awaiting_variant || awaiting_segment) {
        return std::unexpected(make_error("hls_truncated", "Playlist ended before a referenced variant or segment URI"));
    }
    if (manifest.master_playlist && manifest.variants.empty()) return std::unexpected(make_error("hls_master_empty", "HLS master playlist contains no variants"));
    return manifest;
}

} // namespace vortex
