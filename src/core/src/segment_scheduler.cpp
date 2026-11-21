#include "vortex/segment_scheduler.hpp"

#include <algorithm>
#include <utility>
#include <limits>

namespace vortex {

Result<std::vector<ByteRange>> make_byte_ranges(
    std::uint64_t total_bytes,
    std::size_t max_connections,
    std::uint64_t minimum_segment_bytes) {
    if (max_connections == 0U || max_connections > 32U) {
        return std::unexpected(make_error("invalid_connection_count", "Connections must be between 1 and 32"));
    }
    if (minimum_segment_bytes == 0U) {
        return std::unexpected(make_error("invalid_segment_size", "Minimum segment size must be greater than zero"));
    }
    if (total_bytes == 0U) return std::vector<ByteRange>{};

    const std::uint64_t desired = 1U + (total_bytes - 1U) / minimum_segment_bytes;
    const std::size_t count = std::min<std::size_t>(
        max_connections, static_cast<std::size_t>(std::min<std::uint64_t>(desired, max_connections)));
    const std::uint64_t base_size = total_bytes / static_cast<std::uint64_t>(count);
    const std::uint64_t remainder = total_bytes % static_cast<std::uint64_t>(count);

    std::vector<ByteRange> result;
    result.reserve(count);
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint64_t length = base_size + (static_cast<std::uint64_t>(index) < remainder ? 1U : 0U);
        result.push_back(ByteRange{static_cast<std::uint64_t>(index), offset, offset + length - 1U});
        offset += length;
    }
    return result;
}

Result<std::vector<ByteRange>> plan_recovery_ranges(
    std::uint64_t total_bytes,
    std::vector<ByteRangeCheckpoint> checkpoints) {
    if (total_bytes == 0U) {
        if (!checkpoints.empty()) return std::unexpected(make_error("recovery_journal", "Empty downloads cannot contain segment checkpoints"));
        return std::vector<ByteRange>{};
    }
    std::sort(checkpoints.begin(), checkpoints.end(), [](const auto& left, const auto& right) {
        return left.start < right.start;
    });
    std::vector<ByteRange> missing;
    std::uint64_t cursor = 0U;
    std::uint64_t next_id = 0U;
    for (const auto& checkpoint : checkpoints) {
        if (checkpoint.end < checkpoint.start || checkpoint.end >= total_bytes) {
            return std::unexpected(make_error("recovery_journal", "Segment checkpoint is outside the download bounds"));
        }
        if (checkpoint.start < cursor) {
            return std::unexpected(make_error("recovery_journal", "Segment checkpoints overlap or are duplicated"));
        }
        if (checkpoint.start > cursor) {
            missing.push_back(ByteRange{next_id++, cursor, checkpoint.start - 1U});
        }
        const std::uint64_t length = checkpoint.end - checkpoint.start + 1U;
        if (checkpoint.downloaded_bytes > length) {
            return std::unexpected(make_error("recovery_journal", "Segment checkpoint exceeds its byte interval"));
        }
        if (checkpoint.downloaded_bytes < length) {
            const auto remaining_start = checkpoint.start + checkpoint.downloaded_bytes;
            missing.push_back(ByteRange{next_id++, remaining_start, checkpoint.end});
        }
        cursor = checkpoint.end + 1U;
    }
    if (cursor < total_bytes) missing.push_back(ByteRange{next_id, cursor, total_bytes - 1U});
    return missing;
}

std::optional<ByteRange> steal_tail(ByteRange& active, std::uint64_t minimum_chunk_bytes) {
    const std::uint64_t length = active.size();
    if (minimum_chunk_bytes == 0U || length / 2U < minimum_chunk_bytes) return std::nullopt;

    const std::uint64_t stolen_length = length / 2U;
    const std::uint64_t stolen_start = active.end - stolen_length + 1U;
    const ByteRange stolen{active.id + 1U, stolen_start, active.end};
    active.end = stolen_start - 1U;
    return stolen;
}

} // namespace vortex
