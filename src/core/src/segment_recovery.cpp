#include "vortex/segment_recovery.hpp"

#include "vortex/segment_checkpoint.hpp"

#include <algorithm>
#include <limits>

namespace vortex {

Result<void> recover_download_segments(
    const std::filesystem::path& partial_path,
    std::uint64_t total_bytes,
    const std::string& stream_id,
    SqliteRepository& repository,
    const RecoveryRangeFetcher& fetch_range) {
    if (stream_id.empty() || !fetch_range) {
        return std::unexpected(make_error("recovery_options", "Stream id and range fetcher are required"));
    }
    if (total_bytes == 0U) return {};
    const auto journal = repository.list_segments(stream_id);
    if (!journal) return std::unexpected(journal.error());
    std::vector<ByteRangeCheckpoint> checkpoints;
    checkpoints.reserve(journal->size());
    std::uint64_t next_index = 0U;
    for (const auto& record : *journal) {
        checkpoints.push_back({record.start_offset, record.end_offset, record.downloaded_bytes});
        if (record.segment_index >= next_index) {
            if (record.segment_index == std::numeric_limits<std::uint64_t>::max()) {
                return std::unexpected(make_error("recovery_journal", "Segment index space exhausted"));
            }
            next_index = record.segment_index + 1U;
        }
    }
    const auto plan = plan_recovery_ranges(total_bytes, std::move(checkpoints));
    if (!plan) return std::unexpected(plan.error());
    if (plan->empty()) return {};

    CrashSafeFileWriter writer(partial_path);
    auto opened = writer.open_existing(total_bytes);
    if (!opened) return opened;
    for (const auto& range : *plan) {
        const auto fetched = fetch_range(writer, range);
        if (!fetched) return std::unexpected(fetched.error());
        if (*fetched > range.size()) {
            return std::unexpected(make_error("recovery_fetch", "Range fetcher reported more bytes than requested"));
        }
        SegmentRecord checkpoint;
        checkpoint.stream_id = stream_id;
        checkpoint.segment_index = next_index++;
        checkpoint.start_offset = range.start;
        checkpoint.end_offset = range.end;
        checkpoint.downloaded_bytes = *fetched;
        checkpoint.status = *fetched == range.size() ? "Completed" : "Partial";
        auto saved = commit_segment_checkpoint(writer, repository, checkpoint);
        if (!saved) return saved;
        if (*fetched != range.size()) {
            return std::unexpected(make_error("recovery_incomplete", "Range fetcher returned a partial interval; recovery can resume again later"));
        }
    }
    return {};
}

} // namespace vortex
