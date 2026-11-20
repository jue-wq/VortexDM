#include "core/ipc/native_message_dispatcher.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace {
std::filesystem::path test_database_path() {
    return std::filesystem::temp_directory_path() / "vortex-native-dispatcher-tests.sqlite";
}
}

TEST_CASE("Native dispatcher acknowledges only after durable enqueue and deduplicates replay") {
    const auto path = test_database_path();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    vortex::SqliteDatabase database;
    REQUIRE(database.open(path).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::NativeMessageDispatcher dispatcher(database);

    const auto request = R"({"type":"add_download","request_id":"req-1","url":"https://example.com/files/a.zip","filename":"C:\\temp\\a.zip"})";
    const auto first = dispatcher.dispatch(request);
    REQUIRE(first.has_value());
    REQUIRE(first->has_value());
    const auto first_ack = nlohmann::json::parse(**first);
    CHECK(first_ack["accepted"] == false);
    CHECK(first_ack["error"].get<std::string>().find("queue integration") != std::string::npos);

    const auto replay = dispatcher.dispatch(request);
    REQUIRE(replay.has_value());
    REQUIRE(replay->has_value());
    CHECK(nlohmann::json::parse(**replay)["accepted"] == false);

    const auto downloads = database.list_downloads();
    REQUIRE(downloads.has_value());
    REQUIRE(downloads->size() == 1);
    CHECK(downloads->front().destination == "a.zip");
    REQUIRE(database.close().has_value());
    std::filesystem::remove(path, ignored);
}

TEST_CASE("Native dispatcher rejects unsafe URLs and explicitly rejects unwired features") {
    const auto path = test_database_path();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    vortex::SqliteDatabase database;
    REQUIRE(database.open(path).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::NativeMessageDispatcher dispatcher(database);

    const auto unsafe = dispatcher.dispatch(
        R"({"type":"add_download","request_id":"req-unsafe","url":"https://user:pass@example.com/file"})");
    REQUIRE(unsafe.has_value());
    REQUIRE(unsafe->has_value());
    CHECK(nlohmann::json::parse(**unsafe)["accepted"] == false);

    const auto batch = dispatcher.dispatch(
        R"({"type":"add_batch","request_id":"req-batch","links":[{"url":"https://example.com/a"}]})");
    REQUIRE(batch.has_value());
    REQUIRE(batch->has_value());
    CHECK(nlohmann::json::parse(**batch)["accepted"] == false);

    const auto handshake = dispatcher.dispatch(
        R"({"type":"handshake","protocol_version":1,"extension_version":"1.0.0","capabilities":[]})");
    REQUIRE(handshake.has_value());
    CHECK_FALSE(handshake->has_value());
    REQUIRE(database.close().has_value());
    std::filesystem::remove(path, ignored);
}
