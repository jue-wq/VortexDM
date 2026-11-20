#include "core/common/crypto_utils.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

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
