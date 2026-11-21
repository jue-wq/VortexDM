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

} // namespace vortex
