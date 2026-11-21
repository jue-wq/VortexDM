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
    REQUIRE(writer.open(path, 8).has_value());

    const std::array first{std::byte{'A'}, std::byte{'B'}};
    const std::array last{std::byte{'G'}, std::byte{'H'}};
    REQUIRE(writer.write_at(0, first).has_value());
    REQUIRE(writer.write_at(6, last).has_value());
    REQUIRE(writer.flush().has_value());
    REQUIRE(writer.close().has_value());

    {
        // Close the reader before removing the file; Windows does not allow
        // deleting a file while an input stream still holds it open.
        std::ifstream input(path, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(input)), {});
        CHECK(content.size() == 8);
        CHECK(content.substr(0, 2) == "AB");
        CHECK(content.substr(6, 2) == "GH");
    }
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
}

TEST_CASE("Offset writer reopens an exactly sized partial file without truncating it") {
    const auto path = unique_path();
    {
        std::ofstream output(path, std::ios::binary);
        output << "abcdefgh";
    }
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open_existing(path, 8).has_value());
    const std::array patch{std::byte{'X'}, std::byte{'Y'}};
    REQUIRE(writer.write_at(2, patch).has_value());
    REQUIRE(writer.close().has_value());
    {
        std::ifstream input(path, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(input)), {});
        CHECK(content == "abXYefgh");
    }
    vortex::OffsetFileWriter wrong_size;
    CHECK_FALSE(wrong_size.open_existing(path, 9).has_value());
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
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
