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
class JsonValidator final {
public:
    explicit JsonValidator(const std::string_view input) : input_(input) {}

    [[nodiscard]] bool validate() {
        skip_whitespace();
        if (!parse_value(0U)) return false;
        skip_whitespace();
        return position_ == input_.size();
    }

private:
    static constexpr std::size_t maximum_depth = 128U;

    void skip_whitespace() {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
            ++position_;
        }
    }

    bool consume(const char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool parse_value(const std::size_t depth) {
        if (depth > maximum_depth || position_ >= input_.size()) return false;
        switch (input_[position_]) {
        case '{': return parse_object(depth + 1U);
        case '[': return parse_array(depth + 1U);
        case '"': return parse_string();
        case 't': return parse_literal("true");
        case 'f': return parse_literal("false");
        case 'n': return parse_literal("null");
        default: return parse_number();
        }
    }

    bool parse_object(const std::size_t depth) {
        ++position_;
        skip_whitespace();
        if (consume('}')) return true;
        for (;;) {
            if (!parse_string()) return false;
            skip_whitespace();
            if (!consume(':')) return false;
            skip_whitespace();
            if (!parse_value(depth)) return false;
            skip_whitespace();
            if (consume('}')) return true;
            if (!consume(',')) return false;
            skip_whitespace();
        }
    }

    bool parse_array(const std::size_t depth) {
        ++position_;
        skip_whitespace();
        if (consume(']')) return true;
        for (;;) {
            if (!parse_value(depth)) return false;
            skip_whitespace();
            if (consume(']')) return true;
            if (!consume(',')) return false;
            skip_whitespace();
        }
    }

    bool parse_string() {
        if (!consume('"')) return false;
        while (position_ < input_.size()) {
            const auto ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') return true;
            if (ch < 0x20U) return false;
            if (ch != '\\') continue;
            if (position_ >= input_.size()) return false;
            const char escaped = input_[position_++];
            if (escaped == '"' || escaped == '\\' || escaped == '/' ||
                escaped == 'b' || escaped == 'f' || escaped == 'n' ||
                escaped == 'r' || escaped == 't') continue;
            if (escaped != 'u' || input_.size() - position_ < 4U) return false;
            for (unsigned int i = 0; i < 4U; ++i) {
                const auto digit = static_cast<unsigned char>(input_[position_++]);
                if (!((digit >= '0' && digit <= '9') ||
                      (digit >= 'a' && digit <= 'f') ||
                      (digit >= 'A' && digit <= 'F'))) return false;
            }
        }
        return false;
    }

    bool parse_literal(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    bool parse_number() {
        const auto start = position_;
        (void)consume('-');
        if (consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') return false;
        } else {
            if (position_ >= input_.size() || input_[position_] < '1' ||
                input_[position_] > '9') return false;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        if (consume('.')) {
            const auto fraction_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == fraction_start) return false;
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const auto exponent_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == exponent_start) return false;
        }
        return position_ > start;
    }

    std::string_view input_;
    std::size_t position_{};
};

[[nodiscard]] bool valid_json(const std::string_view text) {
    return JsonValidator(text).validate();
}

} // namespace

Result<std::vector<std::byte>> NativeMessagingFrame::encode(
    const std::string_view json_payload
) {
    if (json_payload.empty() || json_payload.size() > maximum_payload_bytes ||
        json_payload.size() > std::numeric_limits<std::uint32_t>::max() ||
        !valid_utf8(json_payload) || !valid_json(json_payload)) {
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
    if (!valid_utf8(payload) || !valid_json(payload)) return std::unexpected(VortexError::make(
        ErrorCode::parse_error, "Native messaging payload is not valid UTF-8 JSON"
    ));
    return payload;
}

} // namespace vortex
