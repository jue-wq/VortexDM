#include "vortex/download_queue_worker.hpp"

#include "vortex/download_engine.hpp"
#include "vortex/application_paths.hpp"
#include "vortex/process_runner.hpp"
#include "vortex/segment_recovery.hpp"
#include "vortex/segment_checkpoint.hpp"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <thread>
#include <string_view>

namespace vortex {
namespace {
Result<std::filesystem::path> run_media_extract_task(
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
    const bool playlist = task.task_type == "media_extract_playlist";
    const auto output_template = output_path.parent_path() /
        (output_path.stem().string() + (playlist ? "-%(playlist_index)s-%(title)s" : "") + ".%(ext)s");
    arguments.insert(arguments.end(), {
        "--no-warnings", "--no-call-home", "--no-cache-dir", "--quiet", "--no-progress",
        playlist ? "--yes-playlist" : "--no-playlist", "--newline",
        "--format", "bestvideo*+bestaudio/best", "--merge-output-format", "mkv",
        "--output", output_template.string(), "--print", "after_move:VTX_FILE:%(filepath)s",
        "--", task.url
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
    std::filesystem::path final_path;
    std::size_t output_offset = 0U;
    while (output_offset <= process->output.size()) {
        const auto line_end = process->output.find_first_of("\r\n", output_offset);
        const auto line = process->output.substr(output_offset,
            line_end == std::string::npos ? std::string::npos : line_end - output_offset);
        constexpr std::string_view marker = "VTX_FILE:";
        if (line.starts_with(marker)) final_path = line.substr(marker.size());
        if (line_end == std::string::npos) break;
        output_offset = line_end + 1U;
    }
    if (final_path.empty())
        return std::unexpected(make_error("ytdlp_output_missing", "yt-dlp did not report its final output path"));
    std::error_code error;
    const auto size = std::filesystem::file_size(final_path, error);
    if (error || size == 0U)
        return std::unexpected(make_error("ytdlp_output_missing", "yt-dlp exited successfully but the output file is missing or empty"));
    return final_path;
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
        std::filesystem::path completed_path = output_path;
        Result<void> result;
        if (task.task_type.starts_with("media_extract")) {
            const auto media_result = run_media_extract_task(repository_, task, output_path, cancel_requested_);
            if (!media_result) result = std::unexpected(media_result.error());
            else completed_path = *media_result;
        } else {
            const std::string stream_id = task.id + "-primary";
            bool recovered = false;
            bool stop_for_control = false;
            const auto saved_stream = repository_.get_stream_for_download(task.id);
            if (saved_stream && *saved_stream && (**saved_stream).total_bytes > 0U) {
                const auto& stream = **saved_stream;
                std::error_code file_error;
                const auto existing_size = std::filesystem::file_size(stream.temp_path, file_error);
                if (!file_error && existing_size == stream.total_bytes) {
                    std::jthread recovery_monitor([this, id=task.id](std::stop_token monitor_stop) {
                        while (!monitor_stop.stop_requested() && !cancel_requested_.load(std::memory_order_relaxed)) {
                            const auto status = repository_.get_download_status(id);
                            if (!status || *status != "Downloading") {
                                cancel_requested_.store(true, std::memory_order_relaxed);
                                break;
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        }
                    });
                    const auto resumed = recover_download_segments(
                        stream.temp_path, stream.total_bytes, stream.id, repository_,
                        [this, &task, &options](CrashSafeFileWriter& writer, const ByteRange& range) -> Result<std::uint64_t> {
                            const auto status = repository_.get_download_status(task.id);
                            if (!status || *status != "Downloading") {
                                cancel_requested_.store(true, std::memory_order_relaxed);
                                return std::unexpected(make_error("download_cancelled", "Task was paused or cancelled"));
                            }
                            return download_range_to_writer(task.url, range, stream.total_bytes, writer, options);
                        });
                    recovery_monitor.request_stop();
                    recovery_monitor.join();
                    if (resumed) {
                        const auto status = repository_.get_download_status(task.id);
                        if (status && *status == "Downloading") {
                            recovered = true;
                            completed_path = stream.temp_path;
                        } else {
                            result = std::unexpected(make_error("download_cancelled", "Task was paused or cancelled during recovery"));
                            stop_for_control = true;
                        }
                    } else if (resumed.error().code == "download_cancelled") {
                        result = std::unexpected(resumed.error());
                        stop_for_control = true;
                    }
                }
            }
            if (!recovered && !stop_for_control) {
                cancel_requested_.store(false, std::memory_order_relaxed);
                auto cleared = repository_.clear_segments(stream_id);
                if (!cleared) result = std::unexpected(cleared.error());
                else {
                    options.stream_info_callback = [this, &task, stream_id, output_path](const RemoteFileInfo& info) -> Result<void> {
                        StreamRecord stream;
                        stream.id = stream_id;
                        stream.download_id = task.id;
                        stream.stream_role = "primary";
                        stream.url = info.effective_url;
                        stream.temp_path = output_path.string();
                        stream.total_bytes = info.total_bytes;
                        stream.status = "Downloading";
                        return repository_.save_stream(stream);
                    };
                    options.segment_checkpoint_callback = [this, stream_id](CrashSafeFileWriter& writer, const ByteRange& range, std::uint64_t downloaded) -> Result<void> {
                        SegmentRecord checkpoint;
                        checkpoint.stream_id = stream_id;
                        checkpoint.segment_index = range.id;
                        checkpoint.start_offset = range.start;
                        checkpoint.end_offset = range.end;
                        checkpoint.downloaded_bytes = downloaded;
                        checkpoint.status = downloaded == range.size() ? "Completed" : "Partial";
                        return commit_segment_checkpoint(writer, repository_, checkpoint);
                    };
                    options.reset_segment_checkpoints_callback = [this, stream_id] {
                        return repository_.clear_segments(stream_id);
                    };
                    result = download_file(task.url, output_path, options);
                }
            }
        }
        if (result) {
            std::error_code size_error;
            const auto final_size = std::filesystem::file_size(completed_path, size_error);
            if (!size_error) (void)repository_.update_download_progress(task.id, final_size, final_size);
            (void)repository_.update_download_status(task.id, "Completed");
        } else if (result.error().code == "download_cancelled" || result.error().code == "process_cancelled" || token.stop_requested()) {
            if (!task.task_type.starts_with("media_extract")) {
                const auto stream = repository_.get_stream_for_download(task.id);
                if (stream && *stream) {
                    const auto segments = repository_.list_segments((**stream).id);
                    if (segments) {
                        std::uint64_t durable_bytes = 0U;
                        for (const auto& segment : *segments) durable_bytes += segment.downloaded_bytes;
                        durable_bytes = std::min(durable_bytes, (**stream).total_bytes);
                        (void)repository_.update_download_progress(task.id, durable_bytes, (**stream).total_bytes);
                    }
                }
            }
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
