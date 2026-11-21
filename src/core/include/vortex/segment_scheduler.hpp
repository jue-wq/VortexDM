#pragma once

#include "vortex/error.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vortex {

struct ByteRange {
    std::uint64_t id{0};
    std::uint64_t start{0};
    std::uint64_t end{0};

    [[nodiscard]] std::uint64_t size() const noexcept { return end >= start ? end - start + 1U : 0U; }
};

[[nodiscard]] Result<std::vector<ByteRange>> make_byte_ranges(
    std::uint64_t total_bytes,
    std::size_t max_connections,
    std::uint64_t minimum_segment_bytes = 2U * 1024U * 1024U);

// Splits off the upper half of a large active range and returns it as a stealable task.
[[nodiscard]] std::optional<ByteRange> steal_tail(ByteRange& active, std::uint64_t minimum_chunk_bytes);

} // namespace vortex
