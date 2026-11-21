#include "core/common/crypto_utils.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <limits>

TEST_CASE("CryptoUtils computes SHA-256 for a known input") {
    const auto path = std::filesystem::temp_directory_path() / "vortexdm-sha256-test.txt";
    {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output.good());
        output << "abc";
    }

    const auto result = vortex::CryptoUtils::sha256_file(path);
    std::filesystem::remove(path);

    REQUIRE(result.has_value());
    REQUIRE(*result == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("CryptoUtils hashes an exact file range") {
    const auto path = std::filesystem::temp_directory_path() / "vortexdm-sha256-range-test.txt";
    {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output.good());
        output << "prefix-abc-suffix";
    }
    const auto result = vortex::CryptoUtils::sha256_file_range(path, 7, 3);
    const auto empty = vortex::CryptoUtils::sha256_file_range(path, 0, 0);
    const auto truncated = vortex::CryptoUtils::sha256_file_range(path, 15, 8);
    const auto overflow = vortex::CryptoUtils::sha256_file_range(
        path, std::numeric_limits<std::uint64_t>::max(), 1
    );
    std::filesystem::remove(path);

    REQUIRE(result.has_value());
    CHECK(*result == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(empty.has_value());
    CHECK(*empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_FALSE(truncated.has_value());
    CHECK_FALSE(overflow.has_value());
}
