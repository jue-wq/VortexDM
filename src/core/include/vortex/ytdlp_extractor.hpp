#pragma once

#include "vortex/error.hpp"
#include "vortex/process_runner.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vortex {

struct MediaFormat {
    std::string format_id;
    std::string extension;
    std::string resolution;
    std::string video_codec;
    std::string audio_codec;
    std::string protocol;
    std::optional<std::uint64_t> file_size;
};

struct MediaEntry {
    std::string id;
    std::string title;
    std::string webpage_url;
    std::optional<double> duration_seconds;
    std::vector<MediaFormat> formats;
};

struct MediaMetadata {
    std::string id;
    std::string title;
    std::string webpage_url;
    std::optional<double> duration_seconds;
    bool playlist{false};
    std::vector<MediaEntry> entries;
};

[[nodiscard]] Result<MediaMetadata> parse_ytdlp_metadata_json(std::string_view json_text);
[[nodiscard]] Result<MediaMetadata> extract_ytdlp_metadata(
    const std::string& executable,
    const std::string& url,
    const ProcessOptions& options = {});

} // namespace vortex
