#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace vortex {
enum class TaskState { Added, Queued, Probing, Downloading, Paused, Verifying, Completed, Error, Cancelled };
[[nodiscard]] std::string_view to_string(TaskState state) noexcept;
[[nodiscard]] bool can_transition(TaskState from, TaskState to) noexcept;
struct DownloadTask {
    std::string id;
    std::string url;
    std::string filename;
    TaskState state{TaskState::Added};
    std::uint64_t total_bytes{0};
    std::uint64_t downloaded_bytes{0};
};
} // namespace vortex
