#pragma once

#include "vortex/sqlite_repository.hpp"

#include <thread>

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
    void run(std::stop_token token);
    SqliteRepository& repository_;
    std::jthread thread_;
};

} // namespace vortex
