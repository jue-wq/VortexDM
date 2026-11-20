#pragma once

#include "core/common/result.hpp"
#include "core/storage/download_record.hpp"

namespace vortex {

class DownloadStateMachine final {
public:
    [[nodiscard]] static Status validate_transition(
        DownloadState from, DownloadState to
    ) {
        const bool allowed =
            (from == DownloadState::queued &&
                (to == DownloadState::running || to == DownloadState::cancelled)) ||
            (from == DownloadState::running &&
                (to == DownloadState::paused || to == DownloadState::completed ||
                 to == DownloadState::failed || to == DownloadState::cancelled)) ||
            (from == DownloadState::paused &&
                (to == DownloadState::running || to == DownloadState::cancelled)) ||
            (from == DownloadState::failed &&
                (to == DownloadState::queued || to == DownloadState::cancelled));
        if (!allowed) return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Illegal download state transition"
        ));
        return {};
    }
};

} // namespace vortex
