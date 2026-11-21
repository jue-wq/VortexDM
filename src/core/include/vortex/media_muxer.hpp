#pragma once

#include "vortex/error.hpp"
#include "vortex/process_runner.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vortex {

struct MuxRequest {
    std::filesystem::path video_or_primary_input;
    std::optional<std::filesystem::path> audio_input;
    std::filesystem::path output;
    bool include_subtitles{true};
    bool preserve_metadata{true};
    bool preserve_chapters{true};
};

[[nodiscard]] Result<void> mux_media(
    const std::filesystem::path& ffmpeg_executable,
    const MuxRequest& request,
    const ProcessOptions& process_options = {});

} // namespace vortex
