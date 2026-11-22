#pragma once

#include "vortex/chunk_assembler.hpp"
#include "vortex/sqlite_repository.hpp"

#include <QObject>
#include <QTcpServer>
#include <QString>

class QTcpSocket;

class AndroidLoopbackServer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(quint16 port READ port NOTIFY ready)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY ready)
    Q_PROPERTY(bool running READ running NOTIFY ready)
public:
    explicit AndroidLoopbackServer(vortex::SqliteRepository* repository, QObject* parent = nullptr);
    [[nodiscard]] bool start();
    [[nodiscard]] quint16 port() const noexcept;
    [[nodiscard]] QString apiKey() const;
    [[nodiscard]] bool running() const noexcept;

signals:
    void ready();

private:
    void handleSocket(QTcpSocket* socket);
    QByteArray dispatchHttp(const QByteArray& request);
    QByteArray dispatchIpc(const QByteArray& body);
    vortex::Result<std::string> enqueueDownload(const nlohmann::json& payload);
    vortex::Result<std::size_t> enqueueBatch(const nlohmann::json& payload);
    vortex::Result<std::string> enqueueMediaExtract(const nlohmann::json& payload);

    QTcpServer server_;
    vortex::SqliteRepository* repository_{nullptr};
    vortex::ChunkAssembler chunks_;
    QString api_key_;
};

