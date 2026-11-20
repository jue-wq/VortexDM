#include "core/download/download_state_machine.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Download state machine accepts supported transitions") {
    using vortex::DownloadState;
    using vortex::DownloadStateMachine;

    CHECK(DownloadStateMachine::validate_transition(DownloadState::queued, DownloadState::running).has_value());
    CHECK(DownloadStateMachine::validate_transition(DownloadState::running, DownloadState::paused).has_value());
    CHECK(DownloadStateMachine::validate_transition(DownloadState::paused, DownloadState::running).has_value());
    CHECK(DownloadStateMachine::validate_transition(DownloadState::running, DownloadState::completed).has_value());
    CHECK(DownloadStateMachine::validate_transition(DownloadState::running, DownloadState::failed).has_value());
    CHECK(DownloadStateMachine::validate_transition(DownloadState::failed, DownloadState::queued).has_value());
}

TEST_CASE("Download state machine rejects terminal and invalid transitions") {
    using vortex::DownloadState;
    using vortex::DownloadStateMachine;

    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::completed, DownloadState::running).has_value());
    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::cancelled, DownloadState::queued).has_value());
    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::queued, DownloadState::completed).has_value());
    CHECK_FALSE(DownloadStateMachine::validate_transition(DownloadState::paused, DownloadState::completed).has_value());
}
