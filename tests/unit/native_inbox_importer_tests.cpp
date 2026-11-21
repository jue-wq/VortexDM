#include "core/ipc/native_inbox_importer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace {
std::filesystem::path unique_database_path(const std::string& label) {
    return std::filesystem::temp_directory_path() /
        ("vortex-inbox-import-" + label + "-" +
         std::to_string(std::filesystem::file_time_type::clock::now().time_since_epoch().count()) +
         ".sqlite");
}

void remove_database_files(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
}

vortex::DownloadRecord record(const std::string& url, const std::string& filename) {
    vortex::DownloadRecord value;
    value.url = url;
    value.destination = filename;
    value.created_at_unix = 100;
    value.updated_at_unix = 100;
    return value;
}
}

TEST_CASE("Native inbox importer transfers records once and marks source delivery") {
    const auto inbox_path = unique_database_path("source");
    const auto app_path = unique_database_path("app");
    vortex::SqliteDatabase inbox;
    vortex::SqliteDatabase app;
    REQUIRE(inbox.open(inbox_path).has_value());
    REQUIRE(app.open(app_path).has_value());
    REQUIRE(inbox.migrate().has_value());
    REQUIRE(app.migrate().has_value());

    REQUIRE(inbox.enqueue_download_once(
        "browser-request-1", record("https://example.test/a.bin", "a.bin")).has_value());
    REQUIRE(inbox.enqueue_download_once(
        "browser-request-2", record("https://example.test/b.bin", "b.bin")).has_value());

    const auto first_page = vortex::NativeInboxImporter::import_pending(inbox, app, 1);
    REQUIRE(first_page.has_value());
    CHECK(*first_page == 1);
    auto app_records = app.list_downloads();
    REQUIRE(app_records.has_value());
    REQUIRE(app_records->size() == 1);
    CHECK(app_records->front().url == "https://example.test/a.bin");

    const auto second_page = vortex::NativeInboxImporter::import_pending(inbox, app, 100);
    REQUIRE(second_page.has_value());
    CHECK(*second_page == 1);
    const auto replay = vortex::NativeInboxImporter::import_pending(inbox, app, 100);
    REQUIRE(replay.has_value());
    CHECK(*replay == 0);
    REQUIRE(inbox.mark_native_request_imported("browser-request-1").has_value());
    REQUIRE(inbox.mark_native_request_imported("browser-request-1").has_value());

    app_records = app.list_downloads();
    REQUIRE(app_records.has_value());
    REQUIRE(app_records->size() == 2);

    const auto pending = inbox.list_pending_native_requests(100);
    REQUIRE(pending.has_value());
    CHECK(pending->empty());

    REQUIRE(inbox.close().has_value());
    REQUIRE(app.close().has_value());
    remove_database_files(inbox_path);
    remove_database_files(app_path);
}

TEST_CASE("Native inbox importer safely replays after destination commit before source mark") {
    const auto inbox_path = unique_database_path("recovery-source");
    const auto app_path = unique_database_path("recovery-app");
    vortex::SqliteDatabase inbox;
    vortex::SqliteDatabase app;
    REQUIRE(inbox.open(inbox_path).has_value());
    REQUIRE(app.open(app_path).has_value());
    REQUIRE(inbox.migrate().has_value());
    REQUIRE(app.migrate().has_value());

    const auto download = record("https://example.test/retry.bin", "retry.bin");
    REQUIRE(inbox.enqueue_download_once("browser-request-retry", download).has_value());

    // Simulate a crash after destination commit but before inbox delivery mark.
    REQUIRE(app.enqueue_download_once("browser-request-retry", download).has_value());
    const auto imported = vortex::NativeInboxImporter::import_pending(inbox, app);
    REQUIRE(imported.has_value());
    CHECK(*imported == 1);
    const auto app_records = app.list_downloads();
    REQUIRE(app_records.has_value());
    CHECK(app_records->size() == 1);
    const auto pending = inbox.list_pending_native_requests();
    REQUIRE(pending.has_value());
    CHECK(pending->empty());

    CHECK_FALSE(vortex::NativeInboxImporter::import_pending(inbox, inbox).has_value());
    REQUIRE(inbox.close().has_value());
    REQUIRE(app.close().has_value());
    remove_database_files(inbox_path);
    remove_database_files(app_path);
}
