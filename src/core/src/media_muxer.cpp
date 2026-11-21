#include "vortex/media_muxer.hpp"

#include <string>
#include <vector>

namespace vortex {

Result<void> mux_media(
    const std::filesystem::path& ffmpeg_executable,
    const MuxRequest& request,
    const ProcessOptions& process_options) {
    if (!ffmpeg_executable.is_absolute() || !request.video_or_primary_input.is_absolute() ||
        !request.output.is_absolute() || (request.audio_input && !request.audio_input->is_absolute())) {
        return std::unexpected(make_error("mux_path", "ffmpeg, input and output paths must be absolute"));
    }
    if (request.output == request.video_or_primary_input ||
        (request.audio_input && request.output == *request.audio_input)) {
        return std::unexpected(make_error("mux_path", "Output path must not overwrite an input"));
    }
    std::vector<std::string> arguments{
        "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-i", request.video_or_primary_input.string()
    };
    if (request.audio_input) {
        arguments.insert(arguments.end(), {"-i", request.audio_input->string(),
            "-map", "0:v:0?", "-map", "1:a:0?"});
        if (request.include_subtitles) arguments.insert(arguments.end(), {"-map", "0:s?"});
    } else {
        arguments.insert(arguments.end(), {"-map", "0"});
    }
    arguments.insert(arguments.end(), {"-c", "copy"});
    if (request.preserve_metadata) arguments.insert(arguments.end(), {"-map_metadata", "0"});
    if (request.preserve_chapters) arguments.insert(arguments.end(), {"-map_chapters", "0"});
    arguments.push_back(request.output.string());
    const auto process = run_process(ffmpeg_executable, arguments, process_options);
    if (!process) return std::unexpected(process.error());
    if (process->exit_code != 0) {
        return std::unexpected(make_error("mux_failed", "ffmpeg exited with code " +
            std::to_string(process->exit_code) + ": " + process->output));
    }
    return {};
}

} // namespace vortex
