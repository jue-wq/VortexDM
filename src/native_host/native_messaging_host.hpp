#pragma once

#include "vortex/chunk_assembler.hpp"
#include "vortex/error.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace vortex {

class NativeMessagingHost {
public:
    [[nodiscard]] int run(std::istream& input, std::ostream& output);

private:
    [[nodiscard]] Result<nlohmann::json> dispatch(const nlohmann::json& request);
    [[nodiscard]] Result<void> write_frame(std::ostream& output, const nlohmann::json& value);
    ChunkAssembler chunks_;
};

} // namespace vortex
