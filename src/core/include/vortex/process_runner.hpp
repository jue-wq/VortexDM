#pragma once

#include "vortex/error.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace vortex {

struct ProcessOptions {
    std::chrono::milliseconds timeout{std::chrono::minutes(5)};
    std::size_t max_output_bytes{4U * 1024U * 1024U};
    const std::atomic_bool* cancel_requested{nullptr};
};

struct ProcessResult {
    int exit_code{-1};
    bool timed_out{false};
    bool cancelled{false};
    bool output_truncated{false};
    std::string output;
};

// Executes an absolute executable path directly; no shell is involved.
[[nodiscard]] Result<ProcessResult> run_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const ProcessOptions& options = {});

} // namespace vortex
