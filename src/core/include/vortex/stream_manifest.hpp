#pragma once

#include "vortex/error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vortex {

struct HlsVariant {
    std::string uri;
    std::uint64_t bandwidth{0};
    std::string codecs;
    std::string resolution;
};

struct HlsSegment {
    std::string uri;
    double duration_seconds{0.0};
    std::string title;
};

struct HlsManifest {
    bool master_playlist{false};
    bool live{true};
    std::optional<std::string> aes128_key_uri;
    std::optional<std::string> initialization_uri;
    std::vector<HlsVariant> variants;
    std::vector<HlsSegment> segments;
};

[[nodiscard]] Result<HlsManifest> parse_hls_playlist(
    const std::string& text,
    const std::string& playlist_url);

struct DashTimelineEntry {
    std::optional<std::uint64_t> time;
    std::uint64_t duration{0};
    std::int64_t repeat{0};
};

struct DashSegmentTemplate {
    std::string media;
    std::string initialization;
    std::uint64_t timescale{1};
    std::uint64_t duration{0};
    std::uint64_t start_number{1};
    std::vector<DashTimelineEntry> timeline;
};

struct DashRepresentation {
    std::string id;
    std::string mime_type;
    std::string codecs;
    std::uint64_t bandwidth{0};
    std::uint64_t width{0};
    std::uint64_t height{0};
    std::string base_url;
    std::optional<DashSegmentTemplate> segment_template;
    std::vector<std::string> segment_urls;
    std::optional<std::string> initialization_url;
    std::string segment_base_index_range;
    std::optional<std::string> segment_base_initialization_url;
    std::string segment_base_initialization_range;
    bool has_content_protection{false};
};

struct DashManifest {
    bool dynamic{false};
    std::vector<DashRepresentation> representations;
};

[[nodiscard]] Result<DashManifest> parse_dash_mpd(
    const std::string& xml_text,
    const std::string& manifest_url);

} // namespace vortex
