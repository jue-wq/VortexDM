#pragma once

#include "core/common/result.hpp"

#include <cstdint>
#include <vector>

namespace vortex {

struct ByteRange {
    std::uint64_t first{};
    std::uint64_t last{}; // Inclusive.
    [[nodiscard]] std::uint64_t size() const noexcept { return last - first + 1; }
};

class SegmentPlanner final {
public:
    // Returns balanced, contiguous, non-overlapping inclusive ranges.
    // Unknown/zero sizes and excessive segment counts are rejected.
    [[nodiscard]] static Result<std::vector<ByteRange>> split(
        std::uint64_t total_size, std::uint32_t requested_segments,
        std::uint32_t maximum_segments = 16
    );
};

} // namespace vortex
