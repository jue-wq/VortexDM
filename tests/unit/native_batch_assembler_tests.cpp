#include "core/ipc/native_batch_assembler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

namespace {
vortex::DownloadRecord record(const char* url) {
    vortex::DownloadRecord value;
    value.url = url;
    value.destination = "download.bin";
    return value;
}
}

TEST_CASE("Native batch assembler emits only complete batches in chunk order") {
    vortex::NativeBatchAssembler assembler;
    auto second = assembler.add({"batch-1", 1, 2, {record("https://example.test/b")}});
    REQUIRE(second);
    CHECK_FALSE(second->has_value());
    auto first = assembler.add({"batch-1", 0, 2, {record("https://example.test/a")}});
    REQUIRE(first);
    REQUIRE(first->has_value());
    REQUIRE(first->value().size() == 2);
    CHECK(first->value()[0].url == "https://example.test/a");
    CHECK(first->value()[1].url == "https://example.test/b");
    CHECK(assembler.pending_batches() == 0);
}

TEST_CASE("Native batch assembler rejects duplicates and conflicting metadata") {
    vortex::NativeBatchAssembler assembler;
    REQUIRE(assembler.add({"batch-2", 0, 2, {record("https://example.test/a")}}));
    CHECK_FALSE(assembler.add({"batch-2", 0, 2, {record("https://example.test/a")}}));
    CHECK(assembler.pending_batches() == 0);

    REQUIRE(assembler.add({"batch-3", 0, 2, {record("https://example.test/a")}}));
    CHECK_FALSE(assembler.add({"batch-3", 1, 3, {record("https://example.test/b")}}));
    CHECK(assembler.pending_batches() == 0);
}

TEST_CASE("Native batch assembler expires incomplete batches and enforces bounds") {
    using namespace std::chrono_literals;
    vortex::NativeBatchAssembler::Limits limits;
    limits.max_pending_batches = 1;
    limits.max_downloads_per_batch = 1;
    limits.assembly_timeout = 5s;
    vortex::NativeBatchAssembler assembler(limits);
    const auto start = vortex::NativeBatchAssembler::Clock::time_point{};
    REQUIRE(assembler.add({"stale", 0, 2, {record("https://example.test/a")}}, start));
    auto after_expiry = assembler.add({"fresh", 0, 1, {record("https://example.test/b")}}, start + 6s);
    REQUIRE(after_expiry);
    REQUIRE(after_expiry->has_value());
    CHECK(after_expiry->value().size() == 1);
    CHECK(assembler.pending_batches() == 0);

    CHECK_FALSE(assembler.add({"too-many", 0, 1,
        {record("https://example.test/a"), record("https://example.test/b")}}, start + 7s));
    CHECK_FALSE(assembler.add({"", 0, 1, {record("https://example.test/a")}}, start + 7s));
}


TEST_CASE("Native batch assembler rejects unusable limits and malformed records") {
    vortex::NativeBatchAssembler::Limits limits;
    limits.max_bytes_per_batch = 0;
    vortex::NativeBatchAssembler unusable(limits);
    CHECK_FALSE(unusable.add({"bad-limits", 0, 1,
        {record("https://example.test/a")}}));

    vortex::NativeBatchAssembler assembler;
    auto invalid = record("https://example.test/invalid");
    invalid.total_bytes = 1;
    invalid.downloaded_bytes = 2;
    CHECK_FALSE(assembler.add({"bad-record", 0, 1, {invalid}}));

    invalid = record("https://example.test/invalid-state");
    invalid.state = static_cast<vortex::DownloadState>(99);
    CHECK_FALSE(assembler.add({"bad-state", 0, 1, {invalid}}));
    CHECK(assembler.pending_batches() == 0);
}


TEST_CASE("Native batch assembler accepts the extension maximum of 128 chunks") {
    vortex::NativeBatchAssembler assembler;
    for (std::size_t index = 0; index < 128; ++index) {
        auto result = assembler.add({
            "large-batch", index, 128,
            {record("https://example.test/" + std::to_string(index))}
        });
        REQUIRE(result);
        if (index < 127) {
            CHECK_FALSE(result->has_value());
        } else {
            REQUIRE(result->has_value());
            CHECK(result->value().size() == 128);
        }
    }
}
