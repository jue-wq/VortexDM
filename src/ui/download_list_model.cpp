#include "download_list_model.hpp"

#include "vortex/application_paths.hpp"
#include "vortex/filename_sanitizer.hpp"
#include "vortex/url.hpp"

#include <QFileInfo>
#include <QUuid>

DownloadListModel::DownloadListModel(vortex::SqliteRepository* repository, QObject* parent)
    : QAbstractListModel(parent), repository_(repository) {
    refresh();
    refresh_timer_.setInterval(1000);
    connect(&refresh_timer_, &QTimer::timeout, this, &DownloadListModel::refresh);
    refresh_timer_.start();
}

int DownloadListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant DownloadListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const auto& row = rows_[index.row()];
    switch (role) {
    case Qt::UserRole + 1: return row.id;
    case Qt::UserRole + 2: return row.filename;
    case Qt::UserRole + 3: return row.url;
    case Qt::UserRole + 4: return row.status;
    case Qt::UserRole + 5: return row.queueId;
    case Qt::UserRole + 6: return row.totalBytes;
    case Qt::UserRole + 7: return row.downloadedBytes;
    default: return {};
    }
}

QHash<int, QByteArray> DownloadListModel::roleNames() const {
    return {
        {Qt::UserRole + 1, "taskId"}, {Qt::UserRole + 2, "filename"},
        {Qt::UserRole + 3, "url"}, {Qt::UserRole + 4, "status"},
        {Qt::UserRole + 5, "queueId"}, {Qt::UserRole + 6, "totalBytes"},
        {Qt::UserRole + 7, "downloadedBytes"}
    };
}

int DownloadListModel::count() const noexcept { return rows_.size(); }

bool DownloadListModel::addDownload(const QString& input) {
    if (!repository_) return false;
    const auto url = input.trimmed().toStdString();
    auto parsed = vortex::parse_http_url(url);
    if (!parsed) return false;
    const auto slash = url.find_last_of('/');
    const auto end = url.find_first_of("?#");
    const auto name = slash == std::string::npos ? std::string{} : url.substr(slash + 1U, (end == std::string::npos ? url.size() : end) - slash - 1U);
    vortex::DownloadRecord record;
    record.id = "ui-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    record.url = url;
    record.filename = vortex::sanitize_filename(name.empty() ? "download" : name);
    record.save_dir = vortex::default_download_directory().string();
    record.status = "Queued";
    auto saved = repository_->save_download(record);
    if (!saved) return false;
    refresh();
    return true;
}

bool DownloadListModel::controlTask(const QString& taskId, const QString& action) {
    if (!repository_) return false;
    const auto id = taskId.toStdString();
    const auto current = repository_->get_download_status(id);
    if (!current) return false;
    std::string next;
    if (action == "pause" && (*current == "Queued" || *current == "Downloading")) next = "Paused";
    else if (action == "resume" && *current == "Paused") next = "Queued";
    else if (action == "cancel" && *current != "Completed" && *current != "Cancelled") next = "Cancelled";
    else return false;
    const auto updated = repository_->update_download_status(id, next);
    if (!updated) return false;
    refresh();
    return true;
}

void DownloadListModel::refresh() {
    if (!repository_) return;
    const auto records = repository_->list_downloads();
    if (!records) return;
    QVector<Row> next;
    next.reserve(static_cast<qsizetype>(records->size()));
    for (const auto& record : *records) {
        next.push_back(Row{QString::fromStdString(record.id), QString::fromStdString(record.filename),
            QString::fromStdString(record.url), QString::fromStdString(record.status),
            QString::fromStdString(record.queue_id), record.total_bytes, record.downloaded_bytes});
    }
    beginResetModel();
    rows_ = std::move(next);
    endResetModel();
    emit countChanged();
}
