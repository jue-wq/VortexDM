#pragma once

#include "core/common/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vortex {

class NativeMessagingFrame final {
public:
    static constexpr std::uint32_t maximum_payload_bytes = 1024U * 1024U;

    [[nodiscard]] static Result<std::vector<std::byte>> encode(std::string_view json_payload);
    [[nodiscard]] static Result<std::string> decode(std::span<const std::byte> frame);
};

} // namespace vortex
