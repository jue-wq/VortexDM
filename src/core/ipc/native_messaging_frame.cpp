#include "native_messaging_frame.hpp"

#include <limits>


namespace vortex {
namespace {
[[nodiscard]] bool is_continuation(const unsigned char value) {
    return value >= 0x80U && value <= 0xbfU;
}
[[nodiscard]] bool valid_utf8(const std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7fU) { ++index; continue; }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (index + 1U >= text.size() ||
                !is_continuation(static_cast<unsigned char>(text[index + 1U]))) return false;
            index += 2U; continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
            if (index + 2U >= text.size()) return false;
            const auto second = static_cast<unsigned char>(text[index + 1U]);
            const auto third = static_cast<unsigned char>(text[index + 2U]);
            const bool valid_second =
                first == 0xe0U ? (second >= 0xa0U && second <= 0xbfU) :
                first == 0xedU ? (second >= 0x80U && second <= 0x9fU) :
                is_continuation(second);
            if (!valid_second || !is_continuation(third)) return false;
            index += 3U; continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
            if (index + 3U >= text.size()) return false;
            const auto second = static_cast<unsigned char>(text[index + 1U]);
            const auto third = static_cast<unsigned char>(text[index + 2U]);
            const auto fourth = static_cast<unsigned char>(text[index + 3U]);
            const bool valid_second =
                first == 0xf0U ? (second >= 0x90U && second <= 0xbfU) :
                first == 0xf4U ? (second >= 0x80U && second <= 0x8fU) :
                is_continuation(second);
            if (!valid_second || !is_continuation(third) ||
                !is_continuation(fourth)) return false;
            index += 4U; continue;
        }
        return false;
    }
    return true;
}
} // namespace

Result<std::vector<std::byte>> NativeMessagingFrame::encode(
    const std::string_view json_payload
) {
    if (json_payload.empty() || json_payload.size() > maximum_payload_bytes ||
        json_payload.size() > std::numeric_limits<std::uint32_t>::max() ||
        !valid_utf8(json_payload)) {
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
    for (const char character : json_payload) {
        const auto value = static_cast<unsigned char>(character);
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
    if (!valid_utf8(payload)) return std::unexpected(VortexError::make(
        ErrorCode::parse_error, "Native messaging payload is not valid UTF-8"
    ));
    return payload;
}

} // namespace vortex
