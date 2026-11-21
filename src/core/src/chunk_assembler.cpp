#include "vortex/chunk_assembler.hpp"

#include <algorithm>
#include <array>

namespace vortex {
namespace {

[[nodiscard]] Result<std::string> decode_base64(std::string_view encoded) {
    static constexpr std::array<signed char, 256> decode_table = [] {
        std::array<signed char, 256> table{};
        table.fill(-1);
        for (int index = 0; index < 26; ++index) {
            table[static_cast<std::size_t>('A' + index)] = static_cast<signed char>(index);
            table[static_cast<std::size_t>('a' + index)] = static_cast<signed char>(index + 26);
        }
        for (int index = 0; index < 10; ++index) {
            table[static_cast<std::size_t>('0' + index)] = static_cast<signed char>(index + 52);
        }
        table[static_cast<std::size_t>('+')] = 62;
        table[static_cast<std::size_t>('/')] = 63;
        return table;
    }();

    if (encoded.empty() || encoded.size() % 4U != 0U) {
        return std::unexpected(make_error("invalid_base64", "Chunk base64 length is invalid"));
    }
    std::string output;
    output.reserve((encoded.size() / 4U) * 3U);
    for (std::size_t offset = 0; offset < encoded.size(); offset += 4U) {
        const auto c0 = static_cast<unsigned char>(encoded[offset]);
        const auto c1 = static_cast<unsigned char>(encoded[offset + 1U]);
        const auto c2 = static_cast<unsigned char>(encoded[offset + 2U]);
        const auto c3 = static_cast<unsigned char>(encoded[offset + 3U]);
        const int a = decode_table[c0];
        const int b = decode_table[c1];
        const int c = c2 == static_cast<unsigned char>('=') ? 0 : decode_table[c2];
        const int d = c3 == static_cast<unsigned char>('=') ? 0 : decode_table[c3];
        const bool final_block = offset + 4U == encoded.size();
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
            (c2 == static_cast<unsigned char>('=') && (!final_block || c3 != static_cast<unsigned char>('='))) ||
            (c3 == static_cast<unsigned char>('=') && !final_block)) {
            return std::unexpected(make_error("invalid_base64", "Chunk contains invalid base64 data"));
        }
        const unsigned int bits = (static_cast<unsigned int>(a) << 18U) |
            (static_cast<unsigned int>(b) << 12U) |
            (static_cast<unsigned int>(c) << 6U) |
            static_cast<unsigned int>(d);
        output.push_back(static_cast<char>((bits >> 16U) & 0xffU));
        if (c2 != static_cast<unsigned char>('=')) output.push_back(static_cast<char>((bits >> 8U) & 0xffU));
        if (c3 != static_cast<unsigned char>('=')) output.push_back(static_cast<char>(bits & 0xffU));
    }
    if (output.empty() || output.size() > 256U * 1024U) {
        return std::unexpected(make_error("chunk_size", "Decoded chunk must be between 1 byte and 256 KiB"));
    }
    return output;
}

} // namespace

void ChunkAssembler::expire(std::chrono::steady_clock::time_point now) {
    constexpr auto lifetime = std::chrono::seconds(30);
    for (auto iterator = pending_.begin(); iterator != pending_.end();) {
        if (now - iterator->second.updated_at > lifetime) iterator = pending_.erase(iterator);
        else ++iterator;
    }
}

std::size_t ChunkAssembler::pending_count() const noexcept {
    return pending_.size();
}

Result<ChunkAssemblyResult> ChunkAssembler::add(
    std::string_view chunk_id,
    std::size_t chunk_index,
    std::size_t chunk_total,
    std::string_view chunk_data_base64,
    std::chrono::steady_clock::time_point now) {
    expire(now);
    if (chunk_id.empty() || chunk_id.size() > 128U || chunk_total == 0U ||
        chunk_total > 128U || chunk_index >= chunk_total) {
        return std::unexpected(make_error("invalid_chunk", "Chunk identity, index or total is invalid"));
    }
    if (pending_.size() >= 8U && pending_.find(std::string(chunk_id)) == pending_.end()) {
        return std::unexpected(make_error("chunk_capacity", "Too many batch assemblies are pending"));
    }

    auto decoded = decode_base64(chunk_data_base64);
    if (!decoded) return std::unexpected(decoded.error());

    const std::string key(chunk_id);
    auto iterator = pending_.find(key);
    if (iterator == pending_.end()) {
        Assembly assembly;
        assembly.chunks.resize(chunk_total);
        assembly.received.resize(chunk_total, false);
        assembly.updated_at = now;
        iterator = pending_.emplace(key, std::move(assembly)).first;
    }

    auto& assembly = iterator->second;
    if (assembly.chunks.size() != chunk_total || assembly.received[chunk_index]) {
        pending_.erase(iterator);
        return std::unexpected(make_error("chunk_sequence", "Chunk total mismatch or duplicate chunk received"));
    }
    constexpr std::size_t maximum_payload = 32U * 1024U * 1024U;
    if (decoded->size() > maximum_payload - assembly.total_bytes) {
        pending_.erase(iterator);
        return std::unexpected(make_error("chunk_payload_limit", "Reassembled batch exceeds 32 MiB"));
    }

    assembly.total_bytes += decoded->size();
    assembly.chunks[chunk_index] = std::move(*decoded);
    assembly.received[chunk_index] = true;
    assembly.received_count += 1U;
    assembly.updated_at = now;

    ChunkAssemblyResult result;
    result.received_chunks = assembly.received_count;
    if (assembly.received_count != chunk_total) return result;

    result.payload.reserve(assembly.total_bytes);
    for (const auto& part : assembly.chunks) result.payload.append(part);
    result.complete = true;
    pending_.erase(iterator);
    return result;
}

} // namespace vortex
