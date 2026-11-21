#pragma once

#include "vortex/sqlite_repository.hpp"

#include <QAbstractListModel>
#include <QTimer>

class DownloadListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit DownloadListModel(vortex::SqliteRepository* repository, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const noexcept;
    Q_INVOKABLE bool addDownload(const QString& url);
    Q_INVOKABLE void refresh();

signals:
    void countChanged();

private:
    struct Row {
        QString id;
        QString filename;
        QString url;
        QString status;
        QString queueId;
        qulonglong totalBytes{0};
        qulonglong downloadedBytes{0};
    };
    vortex::SqliteRepository* repository_{nullptr};
    QVector<Row> rows_;
    QTimer refresh_timer_;
};
