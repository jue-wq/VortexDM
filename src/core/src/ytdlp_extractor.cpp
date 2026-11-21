#include "vortex/ytdlp_extractor.hpp"

#include "vortex/url.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <string>
#include <utility>

namespace vortex {
namespace {
using Json = nlohmann::json;

std::string string_value(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::optional<double> number_value(const Json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number()) return std::nullopt;
    const double value = found->get<double>();
    return value >= 0.0 ? std::optional<double>(value) : std::nullopt;
}

std::optional<std::uint64_t> size_value(const Json& object) {
    for (const char* key : {"filesize", "filesize_approx"}) {
        const auto found = object.find(key);
        if (found == object.end() || !found->is_number_unsigned() && !found->is_number_integer()) continue;
        const auto signed_value = found->get<std::int64_t>();
        if (signed_value >= 0) return static_cast<std::uint64_t>(signed_value);
    }
    return std::nullopt;
}

std::vector<MediaFormat> parse_formats(const Json& entry) {
    std::vector<MediaFormat> formats;
    const auto found = entry.find("formats");
    if (found == entry.end() || !found->is_array()) return formats;
    for (const auto& item : *found) {
        if (!item.is_object()) continue;
        MediaFormat format;
        format.format_id = string_value(item, "format_id");
        format.extension = string_value(item, "ext");
        format.resolution = string_value(item, "resolution");
        format.video_codec = string_value(item, "vcodec");
        format.audio_codec = string_value(item, "acodec");
        format.protocol = string_value(item, "protocol");
        format.file_size = size_value(item);
        if (!format.format_id.empty()) formats.push_back(std::move(format));
    }
    return formats;
}

MediaEntry parse_entry(const Json& object) {
    MediaEntry entry;
    entry.id = string_value(object, "id");
    entry.title = string_value(object, "title");
    entry.webpage_url = string_value(object, "webpage_url");
    entry.duration_seconds = number_value(object, "duration");
    entry.formats = parse_formats(object);
    return entry;
}
}

Result<MediaMetadata> parse_ytdlp_metadata_json(std::string_view json_text) {
    Json root;
    try {
        root = Json::parse(json_text);
    } catch (const Json::exception& error) {
        return std::unexpected(make_error("ytdlp_json", std::string("Invalid yt-dlp JSON: ") + error.what()));
    }
    if (!root.is_object()) return std::unexpected(make_error("ytdlp_json", "yt-dlp metadata root must be an object"));
    MediaMetadata metadata;
    metadata.id = string_value(root, "id");
    metadata.title = string_value(root, "title");
    metadata.webpage_url = string_value(root, "webpage_url");
    metadata.duration_seconds = number_value(root, "duration");
    const auto entries = root.find("entries");
    metadata.playlist = entries != root.end() && entries->is_array();
    if (metadata.playlist) {
        for (const auto& item : *entries) {
            if (!item.is_object()) continue;
            auto entry = parse_entry(item);
            if (!entry.id.empty() || !entry.title.empty()) metadata.entries.push_back(std::move(entry));
        }
    } else {
        auto entry = parse_entry(root);
        if (!entry.id.empty() || !entry.title.empty()) metadata.entries.push_back(std::move(entry));
    }
    if (metadata.title.empty() && metadata.entries.empty()) {
        return std::unexpected(make_error("ytdlp_metadata", "yt-dlp returned no usable media title or entries"));
    }
    return metadata;
}

Result<MediaMetadata> extract_ytdlp_metadata(
    const std::string& executable,
    const std::string& url,
    const ProcessOptions& options) {
    auto parsed = parse_http_url(url);
    if (!parsed) return std::unexpected(parsed.error());
    const auto process = run_process(executable,
        {"--dump-single-json", "--no-warnings", "--skip-download", "--no-call-home", "--no-cache-dir", "--", url},
        options);
    if (!process) return std::unexpected(process.error());
    if (process->exit_code != 0) {
        return std::unexpected(make_error("ytdlp_exit", "yt-dlp exited with code " + std::to_string(process->exit_code) + ": " + process->output));
    }
    if (process->output_truncated) {
        return std::unexpected(make_error("ytdlp_output_limit", "yt-dlp metadata exceeded the configured output limit"));
    }
    return parse_ytdlp_metadata_json(process->output);
}

} // namespace vortex
