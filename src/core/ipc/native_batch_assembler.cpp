#include "native_batch_assembler.hpp"

#include <algorithm>
#include <utility>

namespace vortex {
namespace {
[[nodiscard]] VortexError invalid_batch(const char* message) {
    return VortexError::make(ErrorCode::invalid_argument, message);
}
}

NativeBatchAssembler::NativeBatchAssembler() : NativeBatchAssembler(Limits{}) {}
NativeBatchAssembler::NativeBatchAssembler(Limits limits) : limits_(std::move(limits)) {}

std::size_t NativeBatchAssembler::estimate_bytes(const DownloadRecord& record) {
    return record.url.size() + record.destination.size() +
           record.checksum_sha256.size() + sizeof(record);
}

void NativeBatchAssembler::expire(const Clock::time_point now) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.expires_at <= now) it = pending_.erase(it);
        else ++it;
    }
}

Result<std::optional<std::vector<DownloadRecord>>> NativeBatchAssembler::add(
    NativeBatchChunk chunk, const Clock::time_point now
) {
    std::scoped_lock lock(mutex_);
    expire(now);

    if (chunk.batch_id.empty() || chunk.batch_id.size() > 128U ||
        chunk.chunk_total == 0U || chunk.chunk_total > limits_.max_chunks_per_batch ||
        chunk.chunk_index >= chunk.chunk_total || chunk.downloads.empty()) {
        return std::unexpected(invalid_batch("Invalid native batch chunk metadata"));
    }

    std::size_t chunk_bytes = 0;
    for (const auto& record : chunk.downloads) {
        if (record.url.empty() || record.destination.empty()) {
            return std::unexpected(invalid_batch("Batch contains an invalid download record"));
        }
        const auto size = estimate_bytes(record);
        if (size > limits_.max_bytes_per_batch -
                       std::min(chunk_bytes, limits_.max_bytes_per_batch)) {
            return std::unexpected(invalid_batch("Native batch exceeds byte limit"));
        }
        chunk_bytes += size;
    }
    if (chunk.downloads.size() > limits_.max_downloads_per_batch) {
        return std::unexpected(invalid_batch("Native batch exceeds download count limit"));
    }

    auto it = pending_.find(chunk.batch_id);
    if (it == pending_.end()) {
        if (pending_.size() >= limits_.max_pending_batches) {
            return std::unexpected(invalid_batch("Too many pending native batches"));
        }
        PendingBatch batch;
        batch.expected_chunks = chunk.chunk_total;
        batch.expires_at = now + limits_.assembly_timeout;
        it = pending_.emplace(chunk.batch_id, std::move(batch)).first;
    } else if (it->second.expected_chunks != chunk.chunk_total) {
        pending_.erase(it);
        return std::unexpected(invalid_batch("Conflicting native batch chunk count"));
    }

    auto& batch = it->second;
    if (batch.chunks.contains(chunk.chunk_index)) {
        pending_.erase(it);
        return std::unexpected(invalid_batch("Duplicate native batch chunk"));
    }
    if (chunk.downloads.size() > limits_.max_downloads_per_batch -
                                     std::min(batch.download_count, limits_.max_downloads_per_batch) ||
        chunk_bytes > limits_.max_bytes_per_batch -
                          std::min(batch.byte_count, limits_.max_bytes_per_batch)) {
        pending_.erase(it);
        return std::unexpected(invalid_batch("Native batch exceeds configured limits"));
    }

    batch.download_count += chunk.downloads.size();
    batch.byte_count += chunk_bytes;
    batch.chunks.emplace(chunk.chunk_index, std::move(chunk.downloads));
    if (batch.chunks.size() != batch.expected_chunks) {
        return std::optional<std::vector<DownloadRecord>>{};
    }

    std::vector<DownloadRecord> assembled;
    assembled.reserve(batch.download_count);
    for (auto& [index, records] : batch.chunks) {
        (void)index;
        for (auto& record : records) assembled.push_back(std::move(record));
    }
    pending_.erase(it);
    return std::optional<std::vector<DownloadRecord>>{std::move(assembled)};
}

std::size_t NativeBatchAssembler::pending_batches() const {
    std::scoped_lock lock(mutex_);
    return pending_.size();
}

} // namespace vortex
