#include "native_message_dispatcher.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace vortex {
namespace {

using Json = nlohmann::json;

[[nodiscard]] VortexError parse_error(std::string message) {
    return VortexError::make(ErrorCode::parse_error, std::move(message));
}

[[nodiscard]] std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

[[nodiscard]] bool valid_http_url(const std::string& url) {
    if (!(url.starts_with("https://") || url.starts_with("http://"))) return false;
    const auto start = url.find("://") + 3U;
    const auto end = url.find_first_of("/?#", start);
    const auto authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;
    for (const char raw : url) {
        const auto ch = static_cast<unsigned char>(raw);
        if (ch <= 0x20U || ch == 0x7fU) return false;
    }
    return true;
}

[[nodiscard]] std::string safe_filename(const std::string& url) {
    const auto query = url.find_first_of("?#");
    const auto path_end = query == std::string::npos ? url.size() : query;
    const auto slash = url.rfind('/', path_end);
    if (slash == std::string::npos || slash + 1U >= path_end) return "download";
    auto name = url.substr(slash + 1U, path_end - slash - 1U);
    if (name == "." || name == "..") return "download";
    return name;
}

[[nodiscard]] std::string make_ack(const std::string& request_id, bool accepted,
                                   const std::string& error = {}) {
    Json ack{{"type", "ack"}, {"request_id", request_id}, {"accepted", accepted}};
    if (!error.empty()) ack["error"] = error.substr(0, 2048U);
    return ack.dump();
}

} // namespace

Result<std::optional<std::string>> NativeMessageDispatcher::dispatch(const std::string_view payload) {
    Json message;
    try {
        message = Json::parse(payload);
    } catch (const Json::exception& error) {
        return std::unexpected(parse_error(std::string("Invalid JSON: ") + error.what()));
    }
    if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
        return std::unexpected(parse_error("Message must be an object with a string type"));
    }

    const auto type = message["type"].get<std::string>();
    if (type == "handshake") {
        if (!message.contains("protocol_version") || message["protocol_version"] != 1 ||
            !message.contains("extension_version") || !message["extension_version"].is_string() ||
            !message.contains("capabilities") || !message["capabilities"].is_array()) {
            return std::unexpected(parse_error("Unsupported or malformed handshake"));
        }
        return std::optional<std::string>{};
    }

    std::string request_id;
    try {
        if (!message.contains("request_id") || !message["request_id"].is_string()) {
            return std::unexpected(parse_error("request_id is required"));
        }
        request_id = message["request_id"].get<std::string>();
    } catch (const Json::exception&) {
        return std::unexpected(parse_error("Invalid request_id"));
    }

    if (type == "add_download") {
        try {
            if (!message.contains("url") || !message["url"].is_string()) {
                return std::optional<std::string>{make_ack(request_id, false, "url is required")};
            }
            const auto url = message["url"].get<std::string>();
            if (!valid_http_url(url)) {
                return std::optional<std::string>{make_ack(request_id, false, "Only safe HTTP(S) URLs are accepted")};
            }
            std::string filename = message.value("filename", safe_filename(url));
            if (filename.empty() || filename.size() > 1024U) {
                return std::optional<std::string>{make_ack(request_id, false, "Invalid filename")};
            }
            // Never accept a path from the browser: only retain a basename.
            const auto separator = filename.find_last_of("/\\");
            if (separator != std::string::npos) filename.erase(0, separator + 1U);
            if (filename.empty() || filename == "." || filename == "..") filename = "download";

            const auto now = unix_now();
            DownloadRecord record;
            record.url = url;
            record.destination = filename;
            record.state = DownloadState::queued;
            record.created_at_unix = now;
            record.updated_at_unix = now;
            const auto inserted = database_.enqueue_download_once(request_id, record);
            if (!inserted) {
                return std::optional<std::string>{make_ack(request_id, false, inserted.error().message)};
            }
            return std::optional<std::string>{make_ack(request_id, true)};
        } catch (const Json::exception& error) {
            return std::optional<std::string>{make_ack(request_id, false, error.what())};
        }
    }

    if (type == "add_batch") {
        return std::optional<std::string>{make_ack(request_id, false,
            "Batch persistence is not enabled in this host build")};
    }
    if (type == "extract_media") {
        return std::optional<std::string>{make_ack(request_id, false,
            "Media extraction is not enabled in this host build")};
    }
    if (type == "task_control") {
        return std::optional<std::string>{make_ack(request_id, false,
            "Task control is not enabled in this host build")};
    }
    return std::optional<std::string>{make_ack(request_id, false, "Unsupported message type")};
}

} // namespace vortex
