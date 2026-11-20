#include "core/storage/offset_file_writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::filesystem::path unique_path() {
    return std::filesystem::temp_directory_path() /
        ("vortexdm-writer-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".part");
}
}

TEST_CASE("Offset writer writes disjoint ranges at requested offsets") {
    const auto path = unique_path();
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 8).has_value());

    const std::array first{std::byte{'A'}, std::byte{'B'}};
    const std::array last{std::byte{'G'}, std::byte{'H'}};
    REQUIRE(writer.write_at(0, first).has_value());
    REQUIRE(writer.write_at(6, last).has_value());
    REQUIRE(writer.flush().has_value());
    REQUIRE(writer.close().has_value());

    std::ifstream input(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(input)), {});
    CHECK(content.size() == 8);
    CHECK(content.substr(0, 2) == "AB");
    CHECK(content.substr(6, 2) == "GH");
    std::filesystem::remove(path);
}

TEST_CASE("Offset writer rejects writes outside preallocated bounds") {
    const auto path = unique_path();
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 4).has_value());
    const std::array bytes{std::byte{'X'}, std::byte{'Y'}};
    CHECK_FALSE(writer.write_at(3, bytes).has_value());
    CHECK_FALSE(writer.write_at(5, std::span<const std::byte>{}).has_value());
    REQUIRE(writer.close().has_value());
    std::filesystem::remove(path);
}

TEST_CASE("Offset writer safely serializes concurrent disjoint segment writes") {
    constexpr std::size_t segment_count = 8;
    constexpr std::size_t segment_size = 4096;
    const auto path = unique_path();
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, segment_count * segment_size).has_value());

    std::vector<std::thread> workers;
    std::vector<bool> succeeded(segment_count, false);
    for (std::size_t segment = 0; segment < segment_count; ++segment) {
        workers.emplace_back([&, segment] {
            std::vector<std::byte> bytes(segment_size,
                static_cast<std::byte>('A' + static_cast<char>(segment)));
            succeeded[segment] = writer.write_at(segment * segment_size, bytes).has_value();
        });
    }
    for (auto& worker : workers) worker.join();

    for (const bool success : succeeded) CHECK(success);
    REQUIRE(writer.flush().has_value());
    REQUIRE(writer.close().has_value());

    std::ifstream input(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(input)), {});
    REQUIRE(content.size() == segment_count * segment_size);
    for (std::size_t segment = 0; segment < segment_count; ++segment) {
        const auto expected = static_cast<char>('A' + static_cast<char>(segment));
        CHECK(content.substr(segment * segment_size, segment_size) ==
              std::string(segment_size, expected));
    }
    std::filesystem::remove(path);
}
