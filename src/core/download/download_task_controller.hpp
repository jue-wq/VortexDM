#pragma once

#include "core/storage/sqlite_database.hpp"

#include <cstdint>
#include <mutex>
#include <stop_token>
#include <unordered_map>

namespace vortex {

// Coordinates durable task-state transitions with in-memory cancellation.
// The transfer executor remains owned by the caller; it receives stop_token(id).
class DownloadTaskController final {
public:
    explicit DownloadTaskController(SqliteDatabase& database) : database_(database) {}

    [[nodiscard]] Status start(std::int64_t id, std::int64_t now) {
        return begin(id, DownloadState::queued, now);
    }

    [[nodiscard]] Status resume(std::int64_t id, std::int64_t now) {
        return begin(id, DownloadState::paused, now);
    }

    [[nodiscard]] Status retry(std::int64_t id, std::int64_t now) {
        std::scoped_lock lock(mutex_);
        // Allocate the cancellation entry before committing the durable retry.
        // This prevents an allocation failure from leaving a running task without
        // a corresponding stop source.
        std::stop_source fresh_source;
        auto [source, inserted] = stop_sources_.try_emplace(id);
        auto status = database_.retry_failed_download(id, now);
        if (!status) {
            if (inserted) stop_sources_.erase(source);
            return status;
        }
        source->second = std::move(fresh_source);
        return {};
    }

    [[nodiscard]] Status pause(std::int64_t id, std::int64_t now) {
        return stop_and_transition(id, DownloadState::paused, now);
    }

    [[nodiscard]] Status cancel(std::int64_t id, std::int64_t now) {
        return stop_and_transition(id, DownloadState::cancelled, now);
    }

    [[nodiscard]] Status complete(std::int64_t id, std::int64_t now) {
        return stop_and_transition(id, DownloadState::completed, now);
    }

    [[nodiscard]] Status fail(std::int64_t id, std::int64_t now) {
        return stop_and_transition(id, DownloadState::failed, now);
    }

    [[nodiscard]] Status update_progress(
        std::int64_t id, std::int64_t bytes, std::int64_t now) {
        std::scoped_lock lock(mutex_);
        const auto record = find_record(id);
        if (!record) return std::unexpected(record.error());
        if (record->state != DownloadState::running) {
            return std::unexpected(VortexError::make(
                ErrorCode::invalid_argument, "Progress can only change while running"));
        }
        return database_.update_download_progress(id, bytes, DownloadState::running, now);
    }

    [[nodiscard]] std::stop_token stop_token(std::int64_t id) const {
        std::scoped_lock lock(mutex_);
        const auto found = stop_sources_.find(id);
        return found == stop_sources_.end() ? std::stop_token{} : found->second.get_token();
    }

private:
    [[nodiscard]] Result<DownloadRecord> find_record(std::int64_t id) {
        auto records = database_.list_downloads();
        if (!records) return std::unexpected(records.error());
        for (const auto& record : *records) {
            if (record.id == id) return record;
        }
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Download does not exist"));
    }

    [[nodiscard]] Status begin(
        std::int64_t id, DownloadState expected, std::int64_t now) {
        std::scoped_lock lock(mutex_);
        return begin_locked(id, expected, now);
    }

    [[nodiscard]] Status begin_locked(
        std::int64_t id, DownloadState expected, std::int64_t now) {
        const auto record = find_record(id);
        if (!record) return std::unexpected(record.error());
        if (record->state != expected) {
            return std::unexpected(VortexError::make(
                ErrorCode::invalid_argument, "Download is not in the expected state"));
        }
        // Reserve the in-memory cancellation entry before changing durable
        // state, so allocation failure cannot strand a running task.
        std::stop_source fresh_source;
        auto [source, inserted] = stop_sources_.try_emplace(id);
        auto status = database_.update_download_progress(
            id, record->downloaded_bytes, DownloadState::running, now);
        if (!status) {
            if (inserted) stop_sources_.erase(source);
            return status;
        }
        source->second = std::move(fresh_source);
        return {};
    }

    [[nodiscard]] Status stop_and_transition(
        std::int64_t id, DownloadState target, std::int64_t now) {
        std::scoped_lock lock(mutex_);
        const auto record = find_record(id);
        if (!record) return std::unexpected(record.error());
        auto status = database_.update_download_progress(
            id, record->downloaded_bytes, target, now);
        if (!status) return status;
        const auto found = stop_sources_.find(id);
        if (found != stop_sources_.end()) found->second.request_stop();
        return {};
    }

    SqliteDatabase& database_;
    mutable std::mutex mutex_;
    std::unordered_map<std::int64_t, std::stop_source> stop_sources_;
};

} // namespace vortex
