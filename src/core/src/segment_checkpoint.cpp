#include "vortex/segment_checkpoint.hpp"

namespace vortex {

Result<void> commit_segment_checkpoint(
    CrashSafeFileWriter& writer,
    SqliteRepository& repository,
    const SegmentRecord& checkpoint) {
    auto durable = writer.flush();
    if (!durable) return durable;
    return repository.save_segment_checkpoint(checkpoint);
}

} // namespace vortex
