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

#include "core/ipc/native_messaging_stream.hpp"
#include <sstream>
TEST_CASE("Native Messaging stream reads consecutive frames and clean EOF") {
 std::stringstream stream;
 REQUIRE(vortex::write_native_message(stream,R"({"type":"one"})").has_value());
 REQUIRE(vortex::write_native_message(stream,R"({"type":"two"})").has_value());
 auto first=vortex::read_native_message(stream); REQUIRE(first.has_value()); REQUIRE(first->has_value()); CHECK(**first==R"({"type":"one"})");
 auto second=vortex::read_native_message(stream); REQUIRE(second.has_value()); REQUIRE(second->has_value()); CHECK(**second==R"({"type":"two"})");
 auto eof=vortex::read_native_message(stream); REQUIRE(eof.has_value()); CHECK_FALSE(eof->has_value());
}
TEST_CASE("Native Messaging stream rejects truncated and oversized frames") {
 std::istringstream short_header(std::string(2,'\0')); CHECK_FALSE(vortex::read_native_message(short_header).has_value());
 std::istringstream short_payload(std::string("\x03\0\0\0{}",6)); CHECK_FALSE(vortex::read_native_message(short_payload).has_value());
 std::istringstream oversized(std::string("\x01\0\x10\0",4)); CHECK_FALSE(vortex::read_native_message(oversized).has_value());
}
