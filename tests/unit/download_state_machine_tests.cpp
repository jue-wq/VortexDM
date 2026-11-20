#include "core/download/download_state_machine.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Download state machine accepts supported transitions") {
    using vortex::DownloadState;
    using vortex::DownloadStateMachine;

    CHECK(DownloadStateMachine::validate_transition(DownloadState::queued, DownloadState::running));
    CHECK(DownloadStateMachine::validate_transition(DownloadState::running, DownloadState::paused));
    CHECK(DownloadStateMachine::validate_transition(DownloadState::paused, DownloadState::running));
    CHECK(DownloadStateMachine::validate_transition(DownloadState::running, DownloadState::completed));
    CHECK(DownloadStateMachine::validate_transition(DownloadState::running, DownloadState::failed));
    CHECK(DownloadStateMachine::validate_transition(DownloadState::failed, DownloadState::queued));
}

TEST_CASE("Download state machine rejects terminal and invalid transitions") {
    using vortex::DownloadState;
    using vortex::DownloadStateMachine;

    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::completed, DownloadState::running));
    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::cancelled, DownloadState::queued));
    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::queued, DownloadState::completed));
    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::paused, DownloadState::completed));
}
