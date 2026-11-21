#include "native_messaging_host.hpp"

#include <array>
#include <limits>
#include <string_view>

namespace vortex {
namespace {
constexpr std::uint32_t kMaxFrame = 1024U * 1024U;

bool read_exact(std::istream& input, char* target, std::size_t length) {
    input.read(target, static_cast<std::streamsize>(length));
    return static_cast<std::size_t>(input.gcount()) == length;
}

std::uint64_t current_timestamp() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

nlohmann::json error_response(const std::string& request_id, const std::string& code, const std::string& message) {
    return {
        {"protocol_version",1},{"request_id",request_id},{"timestamp",current_timestamp()},
        {"type","error"},{"payload",{{"code",code},{"message",message}}}
    };
}
}

Result<void> NativeMessagingHost::write_frame(std::ostream& output, const nlohmann::json& value) {
    const std::string payload = value.dump();
    if (payload.empty() || payload.size() > kMaxFrame) {
        return std::unexpected(make_error("frame_size", "Native Messaging frame is outside protocol limits"));
    }
    const auto size = static_cast<std::uint32_t>(payload.size());
    const std::array<char,4> header{
        static_cast<char>(size & 0xffU),
        static_cast<char>((size >> 8U) & 0xffU),
        static_cast<char>((size >> 16U) & 0xffU),
        static_cast<char>((size >> 24U) & 0xffU)
    };
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    output.flush();
    if (!output) return std::unexpected(make_error("frame_write", "Could not write Native Messaging response"));
    return {};
}

Result<nlohmann::json> NativeMessagingHost::dispatch(const nlohmann::json& request) {
    const auto version = request.value("protocol_version", 0);
    const auto request_id = request.value("request_id", std::string{});
    const auto type = request.value("type", std::string{});
    const auto timestamp = current_timestamp();
    if (version != 1 || request_id.empty() || !request.contains("payload") || !request["payload"].is_object()) {
        return std::unexpected(make_error("invalid_envelope", "Protocol v1, request_id and object payload are required"));
    }
    const auto& payload = request["payload"];

    if (type == "handshake") {
        return nlohmann::json{
            {"protocol_version",1},{"request_id",request_id},{"timestamp",timestamp},{"type","ack"},
            {"payload",{{"accepted",true},{"app_version","0.1.0"},{"theme","Modern Dark"},
                        {"ytdlp_ready",false},{"queues",nlohmann::json::array()}}}
        };
    }
    if (type == "add_batch") {
        if (!payload.contains("items") || !payload["items"].is_array()) {
            return std::unexpected(make_error("invalid_batch", "add_batch requires an items array"));
        }
        return nlohmann::json{
            {"protocol_version",1},{"request_id",request_id},{"timestamp",timestamp},{"type","ack"},
            {"payload",{{"accepted",true},{"count",payload["items"].size()}}}
        };
    }
    if (type == "add_batch_chunk") {
        if (!payload.contains("chunk_id") || !payload["chunk_id"].is_string() ||
            !payload.contains("chunk_index") || !payload["chunk_index"].is_number_unsigned() ||
            !payload.contains("chunk_total") || !payload["chunk_total"].is_number_unsigned() ||
            !payload.contains("chunk_data") || !payload["chunk_data"].is_string()) {
            return std::unexpected(make_error("invalid_chunk", "add_batch_chunk payload is malformed"));
        }
        auto assembled = chunks_.add(
            payload["chunk_id"].get<std::string>(),
            payload["chunk_index"].get<std::size_t>(),
            payload["chunk_total"].get<std::size_t>(),
            payload["chunk_data"].get<std::string>());
        if (!assembled) return std::unexpected(assembled.error());
        std::size_t count = 0;
        if (assembled->complete) {
            try {
                const auto full = nlohmann::json::parse(assembled->payload);
                if (!full.is_object() || !full.contains("items") || !full["items"].is_array()) {
                    return std::unexpected(make_error("invalid_batch", "Reassembled batch JSON has no items array"));
                }
                count = full["items"].size();
            } catch (const std::exception&) {
                return std::unexpected(make_error("invalid_batch_json", "Reassembled batch is not valid JSON"));
            }
        }
        return nlohmann::json{
            {"protocol_version",1},{"request_id",request_id},{"timestamp",timestamp},{"type","ack"},
            {"payload",{{"accepted",true},{"received_chunks",assembled->received_chunks},
                        {"complete",assembled->complete},{"count",count}}}
        };
    }

    if (type == "add_download") {
        if (!payload.contains("url") || !payload["url"].is_string()) {
            return std::unexpected(make_error("invalid_download", "add_download requires a URL"));
        }
        const std::string url = payload["url"].get<std::string>();
        if (!(url.starts_with("http://") || url.starts_with("https://"))) {
            return std::unexpected(make_error("invalid_download", "Only HTTP and HTTPS URLs are allowed"));
        }
        return nlohmann::json{
            {"protocol_version",1},{"request_id",request_id},{"timestamp",timestamp},{"type","ack"},
            {"payload",{{"accepted",true},{"task_id","pending-core"}}}
        };
    }
    if (type == "task_control" || type == "extract_media") {
        return nlohmann::json{
            {"protocol_version",1},{"request_id",request_id},{"timestamp",timestamp},{"type","ack"},
            {"payload",{{"accepted",false},{"code","not_implemented"},{"message","This application service is not implemented yet"}}}
        };
    }
    return std::unexpected(make_error("unknown_message", "Unsupported IPC message type"));
}

int NativeMessagingHost::run(std::istream& input, std::ostream& output) {
    while (true) {
        std::array<char,4> header{};
        if (!read_exact(input, header.data(), header.size())) return input.eof() ? 0 : 1;
        const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[0]));
        const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[1]));
        const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[2]));
        const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[3]));
        const std::uint32_t length = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
        if (length == 0U || length > kMaxFrame) return 2;
        std::string payload(length, '\0');
        if (!read_exact(input, payload.data(), payload.size())) return 3;

        std::string request_id;
        try {
            const auto request = nlohmann::json::parse(payload);
            if (request.is_object()) request_id = request.value("request_id", std::string{});
            auto response = dispatch(request);
            const auto output_message = response ? *response :
                error_response(request_id, response.error().code, response.error().message);
            auto written = write_frame(output, output_message);
            if (!written) return 4;
        } catch (const std::exception&) {
            auto written = write_frame(output, error_response(request_id, "invalid_json", "Invalid JSON envelope"));
            if (!written) return 4;
        }
    }
}

} // namespace vortex
