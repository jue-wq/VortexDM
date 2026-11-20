#include "segment_planner.hpp"

#include <algorithm>
#include <limits>

namespace vortex {

Result<std::vector<ByteRange>> SegmentPlanner::split(
    const std::uint64_t total_size,
    const std::uint32_t requested_segments,
    const std::uint32_t maximum_segments
) {
    if (total_size == 0 || requested_segments == 0 || maximum_segments == 0 ||
        requested_segments > maximum_segments ||
        total_size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid file size or segment count"
        ));
    }

    const auto count = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(total_size, requested_segments)
    );
    const auto base_size = total_size / count;
    const auto remainder = total_size % count;

    std::vector<ByteRange> ranges;
    ranges.reserve(count);
    std::uint64_t next = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto length = base_size + (index < remainder ? 1U : 0U);
        const auto last = next + length - 1;
        ranges.push_back(ByteRange{next, last});
        next = last + 1;
    }
    return ranges;
}

} // namespace vortex
