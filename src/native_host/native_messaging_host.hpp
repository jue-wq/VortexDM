#pragma once

#include "vortex/chunk_assembler.hpp"
#include "vortex/error.hpp"
#include "vortex/sqlite_repository.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <cstdint>
#include <iostream>
#include <string>

namespace vortex {

class NativeMessagingHost {
public:
    explicit NativeMessagingHost(std::filesystem::path database_path = {});
    [[nodiscard]] int run(std::istream& input, std::ostream& output);

private:
    [[nodiscard]] Result<nlohmann::json> dispatch(const nlohmann::json& request);
    [[nodiscard]] Result<void> write_frame(std::ostream& output, const nlohmann::json& value);
    [[nodiscard]] Result<std::string> enqueue_download(const nlohmann::json& payload);
    [[nodiscard]] Result<std::size_t> enqueue_batch(const nlohmann::json& payload);
    ChunkAssembler chunks_;
    std::unique_ptr<SqliteRepository> repository_;
    std::string initialization_error_;
};

} // namespace vortex
