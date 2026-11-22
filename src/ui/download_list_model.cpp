#include "download_list_model.hpp"

#include "vortex/application_paths.hpp"
#include "vortex/filename_sanitizer.hpp"
#include "vortex/url.hpp"

#include <QFileInfo>
#include <QUuid>
#include <QVariantMap>
#include <QStandardPaths>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QNativeInterface>
#endif

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
QVariantList DownloadListModel::queues() const { return queues_; }

bool DownloadListModel::createQueue(const QString& name) {
    if (!repository_ || name.trimmed().isEmpty()) return false;
    vortex::QueueRecord queue;
    queue.id = "queue-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    queue.name = name.trimmed().toStdString();
    queue.max_concurrent = 3U;
    queue.is_active = true;
    const auto saved = repository_->save_queue(queue);
    if (!saved) return false;
    refresh();
    return true;
}

bool DownloadListModel::configureYtDlp(const QString& binaryPath, const QString& pythonPath) {
    if (!repository_) return false;
    const auto binary = binaryPath.trimmed().toStdString();
    const auto python = pythonPath.trimmed().toStdString();
    if (binary.empty() && python.empty()) return false;
    std::error_code error;
    if (!binary.empty() && (!std::filesystem::path(binary).is_absolute() ||
        !std::filesystem::is_regular_file(binary, error) || error)) return false;
    error.clear();
    if (!python.empty() && (!std::filesystem::path(python).is_absolute() ||
        !std::filesystem::is_regular_file(python, error) || error)) return false;
    vortex::YtDlpProfileRecord profile;
    profile.id = "local-active";
    profile.name = "Local yt-dlp";
    profile.source_type = "local";
    profile.local_binary_path = binary;
    profile.python_path = python;
    profile.is_active = true;
    const auto saved = repository_->save_ytdlp_profile(profile);
    if (!saved) return false;
    return true;
}

bool DownloadListModel::addDownload(const QString& input, const QString& queueId) {
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
#ifdef Q_OS_ANDROID
    record.save_dir = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Downloads")).toStdString();
#else
    record.save_dir = vortex::default_download_directory().string();
#endif
    record.queue_id = queueId.isEmpty() ? "default" : queueId.toStdString();
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

bool DownloadListModel::exportDownload(const QString& taskId) {
#ifdef Q_OS_ANDROID
    if (!repository_) return false;
    const auto record = repository_->get_download(taskId.toStdString());
    if (!record || !*record || (**record).status != "Completed") return false;
    const std::filesystem::path source = std::filesystem::path((**record).save_dir) / (**record).filename;
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) return false;
    const auto source_path = QJniObject::fromString(QString::fromStdString(source.string()));
    const auto display_name = QJniObject::fromString(QString::fromStdString(source.filename().string()));
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT") >= 29) {
        const jboolean exported = QJniObject::callStaticMethod<jboolean>(
            "org/vortexdm/VortexForegroundService", "exportToDownloads",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
            context.object<jobject>(), source_path.object<jstring>(), display_name.object<jstring>());
        return exported == JNI_TRUE;
    }
    const auto mime_type = QJniObject::fromString(QStringLiteral("application/octet-stream"));
    const jboolean launched = QJniObject::callStaticMethod<jboolean>(
        "org/vortexdm/VortexActivity", "requestLegacySafExport",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
        source_path.object<jstring>(), display_name.object<jstring>(), mime_type.object<jstring>());
    return launched == JNI_TRUE;
#else
    Q_UNUSED(taskId);
    return false;
#endif
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
    const auto available = repository_->list_queues();
    if (available) {
        QVariantList next_queues;
        for (const auto& queue : *available)
            next_queues.push_back(QVariantMap{{"id",QString::fromStdString(queue.id)},{"name",QString::fromStdString(queue.name)}});
        if (queues_ != next_queues) {
            queues_ = std::move(next_queues);
            emit queuesChanged();
        }
    }
}
