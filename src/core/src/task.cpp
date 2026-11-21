#include "vortex/task.hpp"
namespace vortex {
std::string_view to_string(TaskState state) noexcept {
    switch (state) {
    case TaskState::Added: return "Added"; case TaskState::Queued: return "Queued";
    case TaskState::Probing: return "Probing"; case TaskState::Downloading: return "Downloading";
    case TaskState::Paused: return "Paused"; case TaskState::Verifying: return "Verifying";
    case TaskState::Completed: return "Completed"; case TaskState::Error: return "Error";
    case TaskState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
bool can_transition(TaskState from, TaskState to) noexcept {
    if (to == TaskState::Error || to == TaskState::Cancelled)
        return from != TaskState::Completed && from != TaskState::Cancelled;
    switch (from) {
    case TaskState::Added: return to == TaskState::Queued;
    case TaskState::Queued: return to == TaskState::Probing;
    case TaskState::Probing: return to == TaskState::Downloading;
    case TaskState::Downloading: return to == TaskState::Paused || to == TaskState::Verifying;
    case TaskState::Paused: return to == TaskState::Downloading;
    case TaskState::Verifying: return to == TaskState::Completed;
    case TaskState::Completed: case TaskState::Error: case TaskState::Cancelled: return false;
    }
    return false;
}
} // namespace vortex
