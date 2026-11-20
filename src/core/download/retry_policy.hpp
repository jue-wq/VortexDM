#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace vortex {

struct RetryDecision {
    bool retry{};
    std::chrono::milliseconds delay{};
};

class RetryPolicy final {
public:
    // Retry transient transport failures only. HTTP 4xx (except 408/429)
    // and successful responses are terminal. Retry-After is capped as well.
    [[nodiscard]] static RetryDecision decide(
        std::uint32_t attempt,
        std::uint32_t maximum_retries,
        int http_status,
        std::optional<std::chrono::seconds> retry_after = std::nullopt,
        std::chrono::milliseconds base_delay = std::chrono::milliseconds{500},
        std::chrono::milliseconds maximum_delay = std::chrono::seconds{30}
    ) noexcept;
};

} // namespace vortex
