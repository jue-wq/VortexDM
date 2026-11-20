#include "core/ipc/native_messaging_frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

TEST_CASE("Native messaging frame round-trips a JSON payload") {
    const std::string json = R"({"type":"handshake","protocol_version":1})";
    const auto encoded = vortex::NativeMessagingFrame::encode(json);
    REQUIRE(encoded);
    REQUIRE(encoded->size() == json.size() + 4U);
    CHECK(std::to_integer<unsigned int>((*encoded)[0]) == json.size());
    const auto decoded = vortex::NativeMessagingFrame::decode(*encoded);
    REQUIRE(decoded);
    CHECK(*decoded == json);
}

TEST_CASE("Native messaging frame rejects malformed and oversized lengths") {
    const std::vector<std::byte> short_header{std::byte{1}, std::byte{2}};
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(short_header));

    const std::vector<std::byte> zero_length(4U, std::byte{0});
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(zero_length));

    const std::vector<std::byte> oversized{
        std::byte{1}, std::byte{0}, std::byte{16}, std::byte{0}
    };
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(oversized));

    const auto incomplete = vortex::NativeMessagingFrame::encode(R"({"type":"ack"})");
    REQUIRE(incomplete);
    auto truncated = *incomplete;
    truncated.pop_back();
    CHECK_FALSE(vortex::NativeMessagingFrame::decode(truncated));
    CHECK_FALSE(vortex::NativeMessagingFrame::encode(""));
}
