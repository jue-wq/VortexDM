#pragma once

#include "core/common/result.hpp"
#include "core/storage/download_record.hpp"

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vortex {

struct NativeBatchChunk {
    std::string batch_id;
    std::size_t chunk_index{};
    std::size_t chunk_total{};
    std::vector<DownloadRecord> downloads;
};

class NativeBatchAssembler final {
public:
    using Clock = std::chrono::steady_clock;
    struct Limits {
        std::size_t max_pending_batches{32};
        std::size_t max_chunks_per_batch{128};
        std::size_t max_downloads_per_batch{1000};
        std::size_t max_bytes_per_batch{4U * 1024U * 1024U};
        std::chrono::seconds assembly_timeout{30};
    };

    NativeBatchAssembler();
    explicit NativeBatchAssembler(Limits limits);
    [[nodiscard]] Result<std::optional<std::vector<DownloadRecord>>> add(
        NativeBatchChunk chunk, Clock::time_point now = Clock::now());
    [[nodiscard]] std::size_t pending_batches() const;

private:
    struct PendingBatch {
        std::size_t expected_chunks{};
        std::size_t download_count{};
        std::size_t byte_count{};
        Clock::time_point expires_at;
        std::map<std::size_t, std::vector<DownloadRecord>> chunks;
    };
    void expire(Clock::time_point now);
    [[nodiscard]] static std::size_t estimate_bytes(const DownloadRecord& record);

    Limits limits_;
    mutable std::mutex mutex_;
    std::map<std::string, PendingBatch> pending_;
};

} // namespace vortex
