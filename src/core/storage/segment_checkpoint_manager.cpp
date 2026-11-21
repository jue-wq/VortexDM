#include "segment_checkpoint_manager.hpp"

#include "core/common/crypto_utils.hpp"

#include <limits>
#include <utility>

namespace vortex {
namespace {

[[nodiscard]] bool valid_sha256(const std::string& digest) {
    if (digest.size() != 64) return false;
    for (const char ch : digest) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return true;
}

} // namespace

SegmentCheckpointManager::SegmentCheckpointManager(
    OffsetFileWriter& writer, SqliteDatabase& database, std::filesystem::path partial_path
) : writer_(writer), database_(database), partial_path_(std::move(partial_path)) {}

Status SegmentCheckpointManager::commit_segment(
    const std::int64_t download_id, const std::int32_t segment_index,
    const std::uint64_t offset, const std::span<const std::byte> data,
    const std::string& expected_sha256, const std::int64_t updated_at_unix
) {
    std::scoped_lock lock(mutex_);
    if (download_id <= 0 || segment_index < 0 || data.empty() ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        data.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()) ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
                     static_cast<std::uint64_t>(data.size()) ||
        !valid_sha256(expected_sha256)) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid segment checkpoint commit request"
        ));
    }

    auto status = writer_.write_at(offset, data);
    if (!status) return status;
    status = writer_.flush_durable();
    if (!status) return status;

    const auto actual = CryptoUtils::sha256_file_range(
        partial_path_, offset, static_cast<std::uint64_t>(data.size())
    );
    if (!actual) return std::unexpected(actual.error());
    if (*actual != expected_sha256) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Written segment SHA-256 does not match the expected digest"
    ));

    return database_.record_segment_checkpoint(SegmentCheckpoint{
        download_id, segment_index, static_cast<std::int64_t>(offset),
        static_cast<std::int64_t>(data.size()), expected_sha256, updated_at_unix
    });
}

Result<std::size_t> SegmentCheckpointManager::verify_and_prune(
    const std::int64_t download_id
) {
    std::scoped_lock lock(mutex_);
    const auto checkpoints = database_.list_segment_checkpoints(download_id);
    if (!checkpoints) return std::unexpected(checkpoints.error());

    std::size_t removed = 0;
    for (const auto& checkpoint : *checkpoints) {
        bool valid = valid_sha256(checkpoint.checksum_sha256);
        if (valid) {
            const auto actual = CryptoUtils::sha256_file_range(
                partial_path_, static_cast<std::uint64_t>(checkpoint.offset),
                static_cast<std::uint64_t>(checkpoint.length)
            );
            if (!actual) return std::unexpected(actual.error());
            valid = *actual == checkpoint.checksum_sha256;
        }
        if (!valid) {
            const auto erased = database_.erase_segment_checkpoint(
                download_id, checkpoint.segment_index
            );
            if (!erased) return std::unexpected(erased.error());
            ++removed;
        }
    }
    return removed;
}

} // namespace vortex
