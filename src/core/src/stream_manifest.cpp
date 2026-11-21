#include "vortex/stream_manifest.hpp"

#include "vortex/url.hpp"

#include <curl/curl.h>
#include <tinyxml2.h>

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

namespace {
std::uint64_t xml_uint(const tinyxml2::XMLElement* element, const char* name, std::uint64_t fallback = 0U) {
    if (!element) return fallback;
    std::uint64_t value = fallback;
    if (element->QueryUnsigned64Attribute(name, &value) != tinyxml2::XML_SUCCESS) return fallback;
    return value;
}

std::string xml_attr(const tinyxml2::XMLElement* element, const char* name) {
    if (!element) return {};
    const auto* value = element->Attribute(name);
    return value ? value : "";
}

Result<std::string> resolve_base(const std::string& current, const tinyxml2::XMLElement* parent) {
    const auto* base = parent ? parent->FirstChildElement("BaseURL") : nullptr;
    if (!base || !base->GetText()) return current;
    return resolve_uri(current, trim_line(base->GetText()));
}

std::optional<DashSegmentTemplate> parse_template(
    const tinyxml2::XMLElement* adaptation,
    const tinyxml2::XMLElement* representation) {
    const auto* source = representation ? representation->FirstChildElement("SegmentTemplate") : nullptr;
    if (!source && adaptation) source = adaptation->FirstChildElement("SegmentTemplate");
    if (!source) return std::nullopt;
    DashSegmentTemplate result;
    result.media = xml_attr(source, "media");
    result.initialization = xml_attr(source, "initialization");
    result.timescale = xml_uint(source, "timescale", 1U);
    result.duration = xml_uint(source, "duration", 0U);
    result.start_number = xml_uint(source, "startNumber", 1U);
    return result;
}
}

Result<DashManifest> parse_dash_mpd(const std::string& xml_text, const std::string& manifest_url) {
    auto base = parse_http_url(manifest_url);
    if (!base) return std::unexpected(base.error());
    if (xml_text.size() > 8U * 1024U * 1024U) return std::unexpected(make_error("manifest_size", "DASH MPD exceeds 8 MiB"));
    tinyxml2::XMLDocument document;
    if (document.Parse(xml_text.data(), xml_text.size()) != tinyxml2::XML_SUCCESS) {
        return std::unexpected(make_error("dash_xml", "DASH MPD XML is malformed"));
    }
    const auto* mpd = document.FirstChildElement("MPD");
    if (!mpd) return std::unexpected(make_error("dash_root", "DASH document must have an MPD root element"));
    DashManifest manifest;
    manifest.dynamic = xml_attr(mpd, "type") == "dynamic";
    auto mpd_base = resolve_base(manifest_url, mpd);
    if (!mpd_base) return std::unexpected(mpd_base.error());
    const auto* period = mpd->FirstChildElement("Period");
    if (!period) return std::unexpected(make_error("dash_period", "DASH MPD has no Period"));
    auto period_base = resolve_base(*mpd_base, period);
    if (!period_base) return std::unexpected(period_base.error());
    for (auto* adaptation = period->FirstChildElement("AdaptationSet"); adaptation;
         adaptation = adaptation->NextSiblingElement("AdaptationSet")) {
        auto adaptation_base = resolve_base(*period_base, adaptation);
        if (!adaptation_base) return std::unexpected(adaptation_base.error());
        const bool adaptation_protected = adaptation->FirstChildElement("ContentProtection") != nullptr;
        for (auto* representation = adaptation->FirstChildElement("Representation"); representation;
             representation = representation->NextSiblingElement("Representation")) {
            DashRepresentation item;
            item.id = xml_attr(representation, "id");
            item.mime_type = xml_attr(representation, "mimeType");
            if (item.mime_type.empty()) item.mime_type = xml_attr(adaptation, "mimeType");
            item.codecs = xml_attr(representation, "codecs");
            if (item.codecs.empty()) item.codecs = xml_attr(adaptation, "codecs");
            item.bandwidth = xml_uint(representation, "bandwidth");
            item.width = xml_uint(representation, "width");
            item.height = xml_uint(representation, "height");
            auto representation_base = resolve_base(*adaptation_base, representation);
            if (!representation_base) return std::unexpected(representation_base.error());
            item.base_url = *representation_base;
            item.has_content_protection = adaptation_protected || representation->FirstChildElement("ContentProtection") != nullptr;
            item.segment_template = parse_template(adaptation, representation);
            const auto* segment_list = representation->FirstChildElement("SegmentList");
            if (!segment_list) segment_list = adaptation->FirstChildElement("SegmentList");
            if (segment_list) {
                if (const auto* initialization = segment_list->FirstChildElement("Initialization")) {
                    const auto source_url = xml_attr(initialization, "sourceURL");
                    if (!source_url.empty()) {
                        auto resolved = resolve_uri(item.base_url, source_url);
                        if (!resolved) return std::unexpected(resolved.error());
                        item.initialization_url = *resolved;
                    }
                }
                for (auto* segment = segment_list->FirstChildElement("SegmentURL"); segment;
                     segment = segment->NextSiblingElement("SegmentURL")) {
                    const auto media = xml_attr(segment, "media");
                    if (media.empty()) continue;
                    auto resolved = resolve_uri(item.base_url, media);
                    if (!resolved) return std::unexpected(resolved.error());
                    item.segment_urls.push_back(*resolved);
                }
            }
            const auto* segment_base = representation->FirstChildElement("SegmentBase");
            if (!segment_base) segment_base = adaptation->FirstChildElement("SegmentBase");
            if (segment_base) {
                item.segment_base_index_range = xml_attr(segment_base, "indexRange");
                if (const auto* initialization = segment_base->FirstChildElement("Initialization")) {
                    item.segment_base_index_range = xml_attr(segment_base, "indexRange");
                    const auto range = xml_attr(initialization, "range");
                    if (!range.empty()) item.segment_base_index_range = range;
                }
            }
            if (item.segment_template || !item.segment_urls.empty() || !item.segment_base_index_range.empty() || !item.base_url.empty()) {
                manifest.representations.push_back(std::move(item));
            }
        }
    }
    if (manifest.representations.empty()) return std::unexpected(make_error("dash_representations", "DASH MPD contains no usable representations"));
    return manifest;
}

} // namespace vortex
