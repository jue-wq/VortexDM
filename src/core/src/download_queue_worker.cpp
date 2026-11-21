#include "vortex/download_queue_worker.hpp"

#include "vortex/download_engine.hpp"
#include "vortex/application_paths.hpp"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace vortex {
DownloadQueueWorker::DownloadQueueWorker(SqliteRepository& repository) : repository_(repository) {}
DownloadQueueWorker::~DownloadQueueWorker() { stop(); }

void DownloadQueueWorker::start() {
    if (thread_.joinable()) return;
    cancel_requested_.store(false, std::memory_order_relaxed);
    thread_ = std::jthread([this](std::stop_token token) { run(token); });
}

void DownloadQueueWorker::stop() {
    if (!thread_.joinable()) return;
    cancel_requested_.store(true, std::memory_order_relaxed);
    thread_.request_stop();
    thread_.join();
}

void DownloadQueueWorker::run(std::stop_token token) {
    while (!token.stop_requested()) {
        auto claimed = repository_.claim_next_queued_download();
        if (!claimed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        if (!*claimed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            continue;
        }
        const auto& task = **claimed;
        std::filesystem::path directory = task.save_dir.empty()
            ? vortex::default_download_directory() : std::filesystem::path(task.save_dir);
        std::error_code filesystem_error;
        std::filesystem::create_directories(directory, filesystem_error);
        if (filesystem_error) {
            (void)repository_.update_download_status(task.id, "Error", filesystem_error.message());
            continue;
        }
        if (!token.stop_requested()) cancel_requested_.store(false, std::memory_order_relaxed);
        DownloadOptions options;
        options.max_connections = 8U;
        options.transfer_timeout_ms = 30L * 60L * 1000L;
        options.cancel_requested = &cancel_requested_;
        auto last_persisted = std::make_shared<std::atomic_uint64_t>(0U);
        options.progress_callback = [this, id=task.id, last_persisted](std::uint64_t downloaded, std::uint64_t total) {
            const auto previous = last_persisted->load(std::memory_order_relaxed);
            if (downloaded == 0U || downloaded >= previous + 256U * 1024U ||
                (total > 0U && downloaded >= total)) {
                const auto current_status = repository_.get_download_status(id);
                if (!current_status || *current_status != "Downloading")
                    cancel_requested_.store(true, std::memory_order_relaxed);
                last_persisted->store(downloaded, std::memory_order_relaxed);
                (void)repository_.update_download_progress(id, downloaded, total);
            }
        };
        const auto result = download_file(task.url, directory / task.filename, options);
        if (result) {
            (void)repository_.update_download_status(task.id, "Completed");
        } else if (result.error().code == "download_cancelled" || token.stop_requested()) {
            if (token.stop_requested()) {
                (void)repository_.update_download_status(task.id, "Queued", "Interrupted by application shutdown");
            } else {
                const auto current_status = repository_.get_download_status(task.id);
                if (current_status && *current_status == "Downloading")
                    (void)repository_.update_download_status(task.id, "Queued", "Transfer interrupted");
            }
        } else {
            (void)repository_.update_download_status(task.id, "Error", result.error().code + ": " + result.error().message);
        }
    }
}

} // namespace vortex
