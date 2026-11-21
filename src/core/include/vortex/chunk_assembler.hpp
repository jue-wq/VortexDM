#pragma once

#include "vortex/error.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vortex {

struct ChunkAssemblyResult {
    bool complete{false};
    std::size_t received_chunks{0};
    std::string payload;
};

class ChunkAssembler {
public:
    [[nodiscard]] Result<ChunkAssemblyResult> add(
        std::string_view chunk_id,
        std::size_t chunk_index,
        std::size_t chunk_total,
        std::string_view chunk_data_base64,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    void expire(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    [[nodiscard]] std::size_t pending_count() const noexcept;

private:
    struct Assembly {
        std::vector<std::string> chunks;
        std::vector<bool> received;
        std::size_t received_count{0};
        std::size_t total_bytes{0};
        std::chrono::steady_clock::time_point updated_at;
    };

    std::unordered_map<std::string, Assembly> pending_;
};

} // namespace vortex
