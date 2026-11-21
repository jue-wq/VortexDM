#include "crypto_utils.hpp"

#include <array>
#include <fstream>
#include <memory>
#include <algorithm>
#include <limits>
#include <openssl/evp.h>

namespace vortex {

Result<std::string> CryptoUtils::sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(VortexError::make(
            ErrorCode::io_error, "Unable to open file for SHA-256: " + path.string()
        ));
    }

    using EvpContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    EvpContext context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        return std::unexpected(VortexError::make(
            ErrorCode::internal_error, "Unable to initialize SHA-256 context"
        ));
    }

    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && EVP_DigestUpdate(
                context.get(), buffer.data(), static_cast<std::size_t>(count)
            ) != 1) {
            return std::unexpected(VortexError::make(
                ErrorCode::internal_error, "SHA-256 update failed"
            ));
        }
    }

    if (!input.eof()) {
        return std::unexpected(VortexError::make(
            ErrorCode::io_error, "Error while reading file for SHA-256: " + path.string()
        ));
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1) {
        return std::unexpected(VortexError::make(
            ErrorCode::internal_error, "SHA-256 finalization failed"
        ));
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.resize(static_cast<std::size_t>(digest_size) * 2);
    for (std::size_t i = 0; i < static_cast<std::size_t>(digest_size); ++i) {
        output[i * 2] = hex[digest[i] >> 4];
        output[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    return output;
}


Result<std::string> CryptoUtils::sha256_file_range(
    const std::filesystem::path& path, const std::uint64_t offset, const std::uint64_t length
) {
    if (offset > std::numeric_limits<std::uint64_t>::max() - length ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "SHA-256 file range is out of bounds"
        ));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Unable to open file for ranged SHA-256: " + path.string()
    ));
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Unable to seek to SHA-256 file range"
    ));

    using EvpContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    EvpContext context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        return std::unexpected(VortexError::make(
            ErrorCode::internal_error, "Unable to initialize SHA-256 context"
        ));
    }

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t remaining = length;
    while (remaining > 0) {
        const auto requested = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, buffer.size())
        );
        input.read(buffer.data(), requested);
        const auto count = input.gcount();
        if (count <= 0) return std::unexpected(VortexError::make(
            ErrorCode::io_error, "File ended before the requested SHA-256 range"
        ));
        if (EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) != 1) {
            return std::unexpected(VortexError::make(
                ErrorCode::internal_error, "SHA-256 range update failed"
            ));
        }
        remaining -= static_cast<std::uint64_t>(count);
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1) {
        return std::unexpected(VortexError::make(
            ErrorCode::internal_error, "SHA-256 range finalization failed"
        ));
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string output(static_cast<std::size_t>(digest_size) * 2, '\\0');
    for (std::size_t i = 0; i < static_cast<std::size_t>(digest_size); ++i) {
        output[i * 2] = hex[digest[i] >> 4];
        output[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    return output;
}

} // namespace vortex
