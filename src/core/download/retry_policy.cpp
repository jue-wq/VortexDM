#include "retry_policy.hpp"

#include <algorithm>
#include <limits>

namespace vortex {

RetryDecision RetryPolicy::decide(
    const std::uint32_t attempt,
    const std::uint32_t maximum_retries,
    const int http_status,
    const std::optional<std::chrono::seconds> retry_after,
    const std::chrono::milliseconds base_delay,
    const std::chrono::milliseconds maximum_delay
) noexcept {
    if (attempt >= maximum_retries || base_delay.count() < 0 ||
        maximum_delay.count() < 0 ||
        // Status 0 represents a transport-level network failure.
        (http_status != 0 && (http_status < 100 || http_status > 599)) ||
        (http_status >= 200 && http_status < 400) ||
        (http_status >= 400 && http_status < 500 &&
         http_status != 408 && http_status != 429)) {
        return {};
    }

    if (retry_after && retry_after->count() >= 0) {
        // Compare in a wide floating-point duration to avoid truncating a
        // sub-second cap to zero or overflowing when Retry-After is very large.
        using WideMilliseconds = std::chrono::duration<long double, std::milli>;
        if (WideMilliseconds{*retry_after} >= WideMilliseconds{maximum_delay}) {
            return {true, maximum_delay};
        }
        return {true, std::chrono::duration_cast<std::chrono::milliseconds>(*retry_after)};
    }

    const auto exponent = std::min<std::uint32_t>(attempt, 20U);
    auto delay = base_delay;
    for (std::uint32_t index = 0; index < exponent; ++index) {
        if (delay.count() > maximum_delay.count() / 2) {
            delay = maximum_delay;
            break;
        }
        delay *= 2;
    }
    return {true, std::min(delay, maximum_delay)};
}

} // namespace vortex
