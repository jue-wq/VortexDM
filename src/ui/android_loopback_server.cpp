#include "android_loopback_server.hpp"

#include "vortex/filename_sanitizer.hpp"
#include "vortex/url.hpp"

#include <QHostAddress>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>

using nlohmann::json;

namespace {
std::uint64_t timestamp_now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

QByteArray response(int status, const QByteArray& body, const QByteArray& contentType = "application/json") {
    const QByteArray reason = status == 200 ? "OK" : status == 204 ? "No Content" :
        status == 400 ? "Bad Request" : status == 401 ? "Unauthorized" :
        status == 404 ? "Not Found" : status == 413 ? "Payload Too Large" : "Internal Server Error";
    QByteArray result = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    result += "Content-Type: " + contentType + "\r\n";
    result += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    result += "Access-Control-Allow-Origin: *\r\n";
    result += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    result += "Access-Control-Allow-Headers: Content-Type, X-Vortex-Api-Key\r\n";
    result += "Connection: close\r\n\r\n";
    result += body;
    return result;
}

std::string suggested_name(const std::string& url) {
    const auto end = url.find_first_of("?#");
    const auto clean = url.substr(0U, end);
    const auto slash = clean.find_last_of('/');
    const auto name = slash == std::string::npos ? std::string{} : clean.substr(slash + 1U);
    return name.empty() ? "download" : name;
}
}

AndroidLoopbackServer::AndroidLoopbackServer(vortex::SqliteRepository* repository, QObject* parent)
    : QObject(parent), repository_(repository) {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            auto* socket = server_.nextPendingConnection();
            if (!socket) continue;
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleSocket(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(5000, socket, [socket] {
                if (socket->state() != QAbstractSocket::UnconnectedState) socket->disconnectFromHost();
            });
        }
    });
}

bool AndroidLoopbackServer::start() {
    if (!repository_ || !server_.listen(QHostAddress::LocalHost, 0)) return false;
    const auto first = QRandomGenerator::system()->generate64();
    const auto second = QRandomGenerator::system()->generate64();
    const auto third = QRandomGenerator::system()->generate64();
    const auto fourth = QRandomGenerator::system()->generate64();
    api_key_ = QStringLiteral("%1%2%3%4").arg(first, 16, 16, QLatin1Char('0'))
        .arg(second, 16, 16, QLatin1Char('0')).arg(third, 16, 16, QLatin1Char('0'))
        .arg(fourth, 16, 16, QLatin1Char('0'));
    emit ready();
    return true;
}

quint16 AndroidLoopbackServer::port() const noexcept { return server_.serverPort(); }
QString AndroidLoopbackServer::apiKey() const { return api_key_; }
bool AndroidLoopbackServer::running() const noexcept { return server_.isListening(); }

void AndroidLoopbackServer::handleSocket(QTcpSocket* socket) {
    if (!socket || socket->property("handled").toBool()) return;
    if (socket->peerAddress() != QHostAddress::LocalHost && socket->peerAddress() != QHostAddress::LocalHostIPv6) {
        socket->write(response(401, R"({"error":"loopback_only"})"));
        socket->disconnectFromHost();
        return;
    }
    auto buffer = socket->property("requestBuffer").toByteArray();
    buffer += socket->readAll();
    if (buffer.size() > 1024 * 1024 + 16384) {
        socket->setProperty("handled", true);
        socket->write(response(413, R"({"error":"request_too_large"})"));
        socket->disconnectFromHost();
        return;
    }
    const auto split = buffer.indexOf("\r\n\r\n");
    if (split < 0) { socket->setProperty("requestBuffer", buffer); return; }
    const auto header = buffer.left(split);
    const auto body = buffer.mid(split + 4);
    const auto lines = header.split('\n');
    if (lines.isEmpty()) return;
    const auto request_line = lines.front().trimmed().split(' ');
    if (request_line.size() < 2) {
        socket->setProperty("handled", true);
        socket->write(response(400, R"({"error":"bad_request"})"));
        socket->disconnectFromHost();
        return;
    }
    const QByteArray method = request_line[0];
    const QByteArray path = request_line[1];
    QByteArray key;
    int content_length = 0;
    for (int index = 1; index < lines.size(); ++index) {
        const auto line = lines[index].trimmed();
        const auto colon = line.indexOf(':');
        if (colon < 0) continue;
        const auto name = line.left(colon).trimmed().toLower();
        const auto value = line.mid(colon + 1).trimmed();
        if (name == "x-vortex-api-key") key = value;
        if (name == "content-length") content_length = value.toInt();
    }
    if (content_length < 0 || content_length > 1024 * 1024) {
        socket->setProperty("handled", true);
        socket->write(response(413, R"({"error":"invalid_content_length"})"));
        socket->disconnectFromHost();
        return;
    }
    if (body.size() < content_length) { socket->setProperty("requestBuffer", buffer); return; }
    socket->setProperty("handled", true);
    if (method == "OPTIONS") {
        socket->write(response(204, {}));
        socket->disconnectFromHost();
        return;
    }
    if (key != api_key_.toUtf8()) {
        socket->write(response(401, R"({"error":"invalid_api_key"})"));
        socket->disconnectFromHost();
        return;
    }
    const QByteArray reply = method == "GET" && path == "/health"
        ? response(200, R"({"ok":true,"service":"VortexDM"})")
        : method == "POST" && path == "/ipc"
            ? dispatchIpc(body.left(content_length))
            : response(404, R"({"error":"not_found"})");
    socket->write(reply);
    socket->disconnectFromHost();
}

QByteArray AndroidLoopbackServer::dispatchIpc(const QByteArray& body) {
    json request;
    try { request = json::parse(body.constData(), body.constData() + body.size()); }
    catch (...) { return response(400, R"({"type":"error","payload":{"code":"invalid_json"}})"); }
    const auto request_id = request.value("request_id", std::string{});
    if (request.value("protocol_version", 0) != 1 || request_id.empty() ||
        !request.contains("payload") || !request["payload"].is_object()) {
        return response(400, R"({"type":"error","payload":{"code":"invalid_envelope"}})");
    }
    const auto& payload = request["payload"];
    json answer;
    if (request.value("type", std::string{}) == "handshake") {
        answer = {{"accepted",true},{"app_version","0.1.0"},{"theme","Modern Dark"},
                  {"ytdlp_ready",false},{"queues",json::array()}};
    } else if (request.value("type", std::string{}) == "add_download") {
        const auto task = enqueueDownload(payload);
        if (!task) answer = {{"accepted",false},{"code",task.error().code},{"message",task.error().message}};
        else answer = {{"accepted",true},{"task_id",*task}};
    } else if (request.value("type", std::string{}) == "add_batch") {
        const auto count = enqueueBatch(payload);
        if (!count) answer = {{"accepted",false},{"code",count.error().code},{"message",count.error().message}};
        else answer = {{"accepted",true},{"count",*count}};
    } else if (request.value("type", std::string{}) == "add_batch_chunk") {
        if (!payload.contains("chunk_id") || !payload["chunk_id"].is_string() ||
            !payload.contains("chunk_index") || !payload["chunk_index"].is_number_unsigned() ||
            !payload.contains("chunk_total") || !payload["chunk_total"].is_number_unsigned() ||
            !payload.contains("chunk_data") || !payload["chunk_data"].is_string()) {
            answer = {{"accepted",false},{"code","invalid_chunk"},{"message","Malformed chunk payload"}};
        } else {
            auto assembled = chunks_.add(payload["chunk_id"].get<std::string>(),
                payload["chunk_index"].get<std::size_t>(), payload["chunk_total"].get<std::size_t>(),
                payload["chunk_data"].get<std::string>());
            if (!assembled) answer = {{"accepted",false},{"code",assembled.error().code},{"message",assembled.error().message}};
            else if (!assembled->complete) answer = {{"accepted",true},{"complete",false},{"received_chunks",assembled->received_chunks}};
            else {
                try {
                    const auto full = json::parse(assembled->payload);
                    const auto count = enqueueBatch(full);
                    if (!count) answer = {{"accepted",false},{"code",count.error().code},{"message",count.error().message}};
                    else answer = {{"accepted",true},{"complete",true},{"count",*count}};
                } catch (...) { answer = {{"accepted",false},{"code","invalid_batch_json"}}; }
            }
        }
    } else if (request.value("type", std::string{}) == "task_control") {
        if (!payload.contains("task_id") || !payload["task_id"].is_string() ||
            !payload.contains("action") || !payload["action"].is_string()) {
            answer = {{"accepted",false},{"code","invalid_task_control"},{"message","task_id and action are required"}};
        } else {
            const auto task_id = payload["task_id"].get<std::string>();
            const auto action = payload["action"].get<std::string>();
            const auto current = repository_->get_download_status(task_id);
            if (!current) answer = {{"accepted",false},{"code",current.error().code},{"message",current.error().message}};
            else {
                std::string next;
                if (action == "pause" && (*current == "Queued" || *current == "Downloading")) next = "Paused";
                else if (action == "resume" && *current == "Paused") next = "Queued";
                else if (action == "cancel" && *current != "Completed" && *current != "Cancelled") next = "Cancelled";
                else answer = {{"accepted",false},{"code","invalid_task_transition"},{"message","Task cannot perform the requested action in its current state"}};
                if (!next.empty()) {
                    auto updated = repository_->update_download_status(task_id, next);
                    if (!updated) answer = {{"accepted",false},{"code",updated.error().code},{"message",updated.error().message}};
                    else answer = {{"accepted",true},{"task_id",task_id},{"status",next}};
                }
            }
        }
    } else {
        answer = {{"accepted",false},{"code","not_implemented"},{"message","Service is not implemented"}};
    }
    return response(200, json{{"protocol_version",1},{"request_id",request_id},
        {"timestamp",timestamp_now()},{"type","ack"},{"payload",answer}}.dump().c_str());
}

vortex::Result<std::string> AndroidLoopbackServer::enqueueDownload(const json& payload) {
    if (!repository_ || !payload.contains("url") || !payload["url"].is_string())
        return std::unexpected(vortex::make_error("invalid_download", "A URL is required"));
    const auto url = payload["url"].get<std::string>();
    auto parsed = vortex::parse_http_url(url);
    if (!parsed) return std::unexpected(parsed.error());
    const auto requested = payload.value("filename", std::string{});
    vortex::DownloadRecord record;
    record.id = "android-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    record.url = url;
    record.filename = vortex::sanitize_filename(requested.empty() ? suggested_name(url) : requested);
    record.save_dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString();
    record.queue_id = payload.value("queue_id", std::string{});
    record.status = "Queued";
    record.headers_json = "{}";
    auto saved = repository_->save_download(record);
    if (!saved) return std::unexpected(saved.error());
    return record.id;
}

vortex::Result<std::size_t> AndroidLoopbackServer::enqueueBatch(const json& payload) {
    if (!payload.contains("items") || !payload["items"].is_array())
        return std::unexpected(vortex::make_error("invalid_batch", "items array is required"));
    std::size_t count = 0;
    for (const auto& item : payload["items"]) {
        if (!item.is_object()) continue;
        auto copy = item;
        if (payload.contains("queue_id") && payload["queue_id"].is_string() && !copy.contains("queue_id"))
            copy["queue_id"] = payload["queue_id"];
        auto saved = enqueueDownload(copy);
        if (!saved) return std::unexpected(saved.error());
        ++count;
    }
    return count;
}
