#include "native_messaging_frame.hpp"

#include <limits>

namespace vortex {

Result<std::vector<std::byte>> NativeMessagingFrame::encode(
    const std::string_view json_payload
) {
    if (json_payload.empty() || json_payload.size() > maximum_payload_bytes ||
        json_payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Native messaging payload size is invalid"
        ));
    }
    const auto length = static_cast<std::uint32_t>(json_payload.size());
    std::vector<std::byte> frame;
    frame.reserve(static_cast<std::size_t>(length) + 4U);
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        frame.push_back(static_cast<std::byte>((length >> shift) & 0xffU));
    }
    for (const unsigned char value : json_payload) {
        frame.push_back(static_cast<std::byte>(value));
    }
    return frame;
}

Result<std::string> NativeMessagingFrame::decode(const std::span<const std::byte> frame) {
    if (frame.size() < 4U) return std::unexpected(VortexError::make(
        ErrorCode::parse_error, "Native messaging frame is shorter than its header"
    ));

    std::uint32_t length = 0;
    for (unsigned int index = 0; index < 4U; ++index) {
        length |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(frame[index]))
                  << (index * 8U);
    }
    if (length == 0U || length > maximum_payload_bytes) {
        return std::unexpected(VortexError::make(
            ErrorCode::parse_error, "Native messaging frame payload length is invalid"
        ));
    }
    if (frame.size() != static_cast<std::size_t>(length) + 4U) {
        return std::unexpected(VortexError::make(
            ErrorCode::parse_error, "Native messaging frame is incomplete or has trailing bytes"
        ));
    }

    std::string payload;
    payload.reserve(length);
    for (std::size_t index = 4U; index < frame.size(); ++index) {
        payload.push_back(static_cast<char>(std::to_integer<unsigned char>(frame[index])));
    }
    return payload;
}

} // namespace vortex
