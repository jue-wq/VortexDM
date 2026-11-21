#include <nlohmann/json.hpp>
#include <cstdint>
#include <iostream>
#include <string>
namespace {
constexpr std::uint32_t kMaxFrame = 1024U * 1024U;
bool read_exact(char* target, std::size_t length) {
    std::cin.read(target, static_cast<std::streamsize>(length));
    return static_cast<std::size_t>(std::cin.gcount()) == length;
}
bool write_frame(const nlohmann::json& value) {
    const std::string payload = value.dump();
    if (payload.empty() || payload.size() > kMaxFrame) return false;
    const auto size = static_cast<std::uint32_t>(payload.size());
    const char header[4] = {static_cast<char>(size & 0xffU), static_cast<char>((size >> 8U) & 0xffU),
        static_cast<char>((size >> 16U) & 0xffU), static_cast<char>((size >> 24U) & 0xffU)};
    std::cout.write(header, 4); std::cout.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    std::cout.flush(); return static_cast<bool>(std::cout);
}
}
int main() {
    while (true) {
        char header[4]{};
        if (!read_exact(header, 4)) return std::cin.eof() ? 0 : 1;
        const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[0]));
        const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[1]));
        const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[2]));
        const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(header[3]));
        const std::uint32_t length = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
        if (length == 0U || length > kMaxFrame) return 2;
        std::string payload(length, '\0');
        if (!read_exact(payload.data(), length)) return 3;
        try {
            const auto request = nlohmann::json::parse(payload);
            const auto version = request.value("protocol_version", 0);
            const auto request_id = request.value("request_id", std::string{});
            const auto type = request.value("type", std::string{});
            nlohmann::json response = {{"protocol_version",1},{"request_id",request_id},{"timestamp",0},{"type",version == 1 ? "ack" : "error"}};
            if (version != 1 || request_id.empty()) {
                response["payload"] = {{"code","invalid_envelope"},{"message","Protocol v1 and request_id are required"}};
            } else if (type == "handshake") {
                response["payload"] = {{"accepted",true},{"app_version","0.1.0"},{"theme","Modern Dark"},{"ytdlp_ready",false},{"queues",nlohmann::json::array()}};
            } else {
                response["payload"] = {{"accepted",false},{"code","not_implemented"},{"message","Message handler is not implemented yet"}};
            }
            if (!write_frame(response)) return 4;
        } catch (const std::exception&) {
            nlohmann::json response = {{"protocol_version",1},{"type","error"},{"request_id",""},{"timestamp",0},
                {"payload",{{"code","invalid_json"},{"message","Invalid JSON envelope"}}}};
            if (!write_frame(response)) return 4;
        }
    }
}
