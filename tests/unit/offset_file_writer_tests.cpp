#include "core/storage/offset_file_writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

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
    REQUIRE(writer.open(path, 8));

    const std::array first{std::byte{'A'}, std::byte{'B'}};
    const std::array last{std::byte{'G'}, std::byte{'H'}};
    REQUIRE(writer.write_at(0, first));
    REQUIRE(writer.write_at(6, last));
    REQUIRE(writer.flush());
    REQUIRE(writer.close());

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
    REQUIRE(writer.open(path, 4));
    const std::array bytes{std::byte{'X'}, std::byte{'Y'}};
    CHECK_FALSE(writer.write_at(3, bytes));
    CHECK_FALSE(writer.write_at(5, std::span<const std::byte>{}));
    REQUIRE(writer.close());
    std::filesystem::remove(path);
}
