#pragma once

#include "vortex/sqlite_repository.hpp"

#include <thread>
#include <atomic>

namespace vortex {

class DownloadQueueWorker {
public:
    explicit DownloadQueueWorker(SqliteRepository& repository);
    ~DownloadQueueWorker();
    DownloadQueueWorker(const DownloadQueueWorker&) = delete;
    DownloadQueueWorker& operator=(const DownloadQueueWorker&) = delete;

    void start();
    void stop();

private:
    void run();
    SqliteRepository& repository_;
    std::thread thread_;
    std::atomic_bool stop_requested_{false};
    std::atomic_bool cancel_requested_{false};
};

} // namespace vortex
