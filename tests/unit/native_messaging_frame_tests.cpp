#include "core/ipc/native_messaging_frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

TEST_CASE("Native messaging frame round-trips a JSON payload") {
    const std::string json = R"({"type":"handshake","protocol_version":1})";
    const auto encoded = vortex::NativeMessagingFrame::encode(json);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->size() == json.size() + 4U);
    CHECK(std::to_integer<unsigned int>((*encoded)[0]) == json.size());
    const auto decoded = vortex::NativeMessagingFrame::decode(*encoded);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == json);
}

TEST_CASE("Native messaging frame validates JSON syntax on encode and decode") {
    const auto valid = vortex::NativeMessagingFrame::encode(
        R"({"items":[1,true,null,{"escaped":"line\\nvalue"}]})"
    );
    REQUIRE(valid.has_value());
    CHECK(vortex::NativeMessagingFrame::decode(*valid).has_value());

    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"missing":})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"trailing":1,})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"number":01})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"string":"bad
control"})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"ok":true} garbage)").has_value());

    const std::string invalid_json = "{]";
    std::vector<std::byte> frame{
        static_cast<std::byte>(invalid_json.size()), std::byte{0},
        std::byte{0}, std::byte{0}
    };
    for (const unsigned char ch : invalid_json) {
        frame.push_back(static_cast<std::byte>(ch));
    }
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(frame).has_value());
}

TEST_CASE("Native messaging frame rejects malformed and oversized lengths") {
    const std::vector<std::byte> short_header{std::byte{1}, std::byte{2}};
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(short_header).has_value());

    const std::vector<std::byte> zero_length(4U, std::byte{0});
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(zero_length).has_value());

    const std::vector<std::byte> oversized{
        std::byte{1}, std::byte{0}, std::byte{16}, std::byte{0}
    };
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(oversized).has_value());

    const auto incomplete = vortex::NativeMessagingFrame::encode(R"({"type":"ack"})");
    REQUIRE(incomplete.has_value());
    auto truncated = *incomplete;
    truncated.pop_back();
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(truncated).has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode("").has_value());
    const char invalid_bytes[] = {static_cast<char>(0xc0), static_cast<char>(0xaf)};
    const std::string invalid_utf8(invalid_bytes, sizeof(invalid_bytes));
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(invalid_utf8).has_value());
}

TEST_CASE("Native messaging JSON validator enforces nesting and number grammar") {
    std::string at_limit(128U, '[');
    at_limit += "0";
    at_limit.append(128U, ']');
    CHECK(vortex::NativeMessagingFrame::encode(at_limit).has_value());

    std::string over_limit(129U, '[');
    over_limit += "0";
    over_limit.append(129U, ']');
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(over_limit).has_value());

    CHECK(vortex::NativeMessagingFrame::encode(R"({"integer":-12,"fraction":0.25,"exponent":1.2e-3})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"fraction":1.})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"exponent":1e+})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"integer":-})").has_value());
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(R"({"integer":+1})").has_value());
}
