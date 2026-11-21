#include "vortex/download_queue_worker.hpp"

#include "vortex/download_engine.hpp"
#include "vortex/application_paths.hpp"
#include "vortex/process_runner.hpp"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <thread>

namespace vortex {
namespace {
Result<void> run_media_extract_task(
    SqliteRepository& repository,
    const DownloadRecord& task,
    const std::filesystem::path& output_path,
    std::atomic_bool& cancel_flag) {
    const auto profiles = repository.list_ytdlp_profiles();
    if (!profiles) return std::unexpected(profiles.error());
    const auto profile = std::find_if(profiles->begin(), profiles->end(), [](const auto& value) {
        return value.is_active && (!value.local_binary_path.empty() || !value.python_path.empty());
    });
    if (profile == profiles->end())
        return std::unexpected(make_error("ytdlp_not_configured", "No active yt-dlp executable profile is configured"));
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    if (!profile->local_binary_path.empty()) {
        executable = profile->local_binary_path;
    } else {
        executable = profile->python_path;
        arguments = {"-m", "yt_dlp"};
    }
    if (!executable.is_absolute())
        return std::unexpected(make_error("ytdlp_path", "Configured yt-dlp executable/Python path must be absolute"));
    arguments.insert(arguments.end(), {
        "--no-warnings", "--no-call-home", "--no-cache-dir",
        task.task_type == "media_extract_playlist" ? "--yes-playlist" : "--no-playlist", "--newline",
        "--format", "bestvideo*+bestaudio/best", "--merge-output-format", "mkv",
        "--output", output_path.string(), "--", task.url
    });
    ProcessOptions options;
    options.timeout = std::chrono::hours(3);
    options.max_output_bytes = 2U * 1024U * 1024U;
    options.cancel_requested = &cancel_flag;
    std::jthread status_monitor([&repository, id=task.id, &cancel_flag](std::stop_token stop) {
        while (!stop.stop_requested() && !cancel_flag.load(std::memory_order_relaxed)) {
            const auto status = repository.get_download_status(id);
            if (!status || *status != "Downloading") {
                cancel_flag.store(true, std::memory_order_relaxed);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
    const auto process = run_process(executable, arguments, options);
    status_monitor.request_stop();
    status_monitor.join();
    if (!process) return std::unexpected(process.error());
    if (process->exit_code != 0)
        return std::unexpected(make_error("ytdlp_failed", "yt-dlp exited with code " + std::to_string(process->exit_code) + ": " + process->output));
    std::error_code error;
    const auto size = std::filesystem::file_size(output_path, error);
    if (error || size == 0U)
        return std::unexpected(make_error("ytdlp_output_missing", "yt-dlp exited successfully but no output file was found"));
    return {};
}
}

DownloadQueueWorker::DownloadQueueWorker(SqliteRepository& repository) : repository_(repository) {}
DownloadQueueWorker::~DownloadQueueWorker() { stop(); }

void DownloadQueueWorker::start() {
    if (thread_.joinable()) return;
    (void)repository_.reset_interrupted_downloads();
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
        const auto output_path = directory / task.filename;
        const auto result = task.task_type.starts_with("media_extract")
            ? run_media_extract_task(repository_, task, output_path, cancel_requested_)
            : download_file(task.url, output_path, options);
        if (result) {
            std::error_code size_error;
            const auto final_size = std::filesystem::file_size(output_path, size_error);
            if (!size_error) (void)repository_.update_download_progress(task.id, final_size, final_size);
            (void)repository_.update_download_status(task.id, "Completed");
        } else if (result.error().code == "download_cancelled" || result.error().code == "process_cancelled" || token.stop_requested()) {
            if (token.stop_requested()) {
                (void)repository_.update_download_status(task.id, "Queued", "Interrupted by application shutdown");
            } else {
                const auto current_status = repository_.get_download_status(task.id);
                if (current_status && *current_status == "Downloading")
                    (void)repository_.update_download_status(task.id, "Queued", "Transfer interrupted");
            }
        } else {
            (void)repository_.update_download_progress(task.id, 0U, 0U);
            (void)repository_.update_download_status(task.id, "Error", result.error().code + ": " + result.error().message);
        }
    }
}

} // namespace vortex
