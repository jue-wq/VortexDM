#include "vortex/ytdlp_toolchain.hpp"

#include <openssl/evp.h>

#include <array>
#include <cctype>
#include <fstream>
#include <memory>
#include <system_error>

namespace vortex {
namespace {
bool valid_digest(const std::string& value) {
    if (value.size() != 64U) return false;
    for (const unsigned char ch : value) if (!std::isxdigit(ch)) return false;
    return true;
}
std::string lower_hex(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
std::filesystem::path sidecar(const std::filesystem::path& path, const char* suffix) {
    auto result = path;
    result += suffix;
    return result;
}
}

Result<std::string> sha256_file(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return std::unexpected(make_error("tool_file", "Tool binary is not a readable regular file"));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::unexpected(make_error("tool_file", "Could not open tool binary for hashing"));
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        return std::unexpected(make_error("tool_hash", "Could not initialize SHA-256 context"));
    }
    std::array<char, 64U * 1024U> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) != 1) {
            return std::unexpected(make_error("tool_hash", "Could not update SHA-256 digest"));
        }
    }
    if (!input.eof()) return std::unexpected(make_error("tool_read", "Could not read complete tool binary"));
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1 || length != 32U) {
        return std::unexpected(make_error("tool_hash", "Could not finalize SHA-256 digest"));
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (unsigned int index = 0; index < length; ++index) {
        result.push_back(hex[digest[index] >> 4U]);
        result.push_back(hex[digest[index] & 0x0fU]);
    }
    return result;
}

Result<void> verify_sha256_file(const std::filesystem::path& path, const std::string& expected_hex) {
    if (!valid_digest(expected_hex)) return std::unexpected(make_error("tool_digest", "Expected SHA-256 must be 64 hexadecimal characters"));
    const auto actual = sha256_file(path);
    if (!actual) return std::unexpected(actual.error());
    if (*actual != lower_hex(expected_hex)) return std::unexpected(make_error("tool_digest_mismatch", "Tool binary SHA-256 does not match the pinned digest"));
    return {};
}

Result<void> activate_verified_tool(
    const std::filesystem::path& candidate,
    const std::filesystem::path& active_path,
    const std::string& expected_sha256) {
    if (!candidate.is_absolute() || !active_path.is_absolute() || candidate == active_path) {
        return std::unexpected(make_error("tool_path", "Candidate and active paths must be distinct absolute paths"));
    }
    auto verified = verify_sha256_file(candidate, expected_sha256);
    if (!verified) return verified;
    std::error_code error;
    const auto parent = active_path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    if (error) return std::unexpected(make_error("tool_directory", error.message()));
    const auto staged = sidecar(active_path, ".new");
    const auto backup = sidecar(active_path, ".rollback");
    std::filesystem::remove(staged, error);
    error.clear();
    std::filesystem::copy_file(candidate, staged, std::filesystem::copy_options::overwrite_existing, error);
    if (error) return std::unexpected(make_error("tool_stage", error.message()));
    auto staged_verified = verify_sha256_file(staged, expected_sha256);
    if (!staged_verified) {
        std::filesystem::remove(staged, error);
        return staged_verified;
    }
    const bool had_active = std::filesystem::exists(active_path, error) && !error;
    if (had_active) {
        std::filesystem::remove(backup, error);
        error.clear();
        std::filesystem::rename(active_path, backup, error);
        if (error) {
            std::filesystem::remove(staged, error);
            return std::unexpected(make_error("tool_backup", "Could not retain previous tool binary: " + error.message()));
        }
    }
    std::filesystem::rename(staged, active_path, error);
    if (error) {
        if (had_active) {
            std::error_code restore_error;
            std::filesystem::rename(backup, active_path, restore_error);
        }
        std::filesystem::remove(staged, error);
        return std::unexpected(make_error("tool_promote", "Could not activate verified tool binary"));
    }
    auto active_verified = verify_sha256_file(active_path, expected_sha256);
    if (!active_verified) {
        std::filesystem::remove(active_path, error);
        if (had_active) {
            std::error_code restore_error;
            std::filesystem::rename(backup, active_path, restore_error);
        }
        return active_verified;
    }
    return {};
}

Result<void> rollback_tool(const std::filesystem::path& active_path) {
    if (!active_path.is_absolute()) return std::unexpected(make_error("tool_path", "Active tool path must be absolute"));
    const auto backup = sidecar(active_path, ".rollback");
    std::error_code error;
    if (!std::filesystem::is_regular_file(backup, error) || error) {
        return std::unexpected(make_error("tool_rollback_missing", "No previous verified tool binary is available"));
    }
    const auto failed = sidecar(active_path, ".failed");
    std::filesystem::remove(failed, error);
    error.clear();
    if (std::filesystem::exists(active_path, error) && !error) {
        std::filesystem::rename(active_path, failed, error);
        if (error) return std::unexpected(make_error("tool_rollback", "Could not preserve current tool binary"));
    }
    std::filesystem::rename(backup, active_path, error);
    if (error) {
        std::error_code restore_error;
        if (std::filesystem::exists(failed, restore_error) && !restore_error)
            std::filesystem::rename(failed, active_path, restore_error);
        return std::unexpected(make_error("tool_rollback", "Could not restore previous tool binary"));
    }
    return {};
}

} // namespace vortex
