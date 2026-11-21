#include <QGuiApplication>
#include <QIcon>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include "vortex/sqlite_repository.hpp"
#include "vortex/download_queue_worker.hpp"
#include "vortex/application_paths.hpp"
#ifdef Q_OS_ANDROID
#include "android_loopback_server.hpp"
#endif

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/vortexdm-32.png")));

#ifdef Q_OS_ANDROID
    const auto data_directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data_directory);
    const std::filesystem::path database_path = (data_directory + QStringLiteral("/vortexdm.sqlite3")).toStdString();
#else
    const std::filesystem::path database_path = vortex::application_database_path();
    std::filesystem::create_directories(database_path.parent_path());
#endif
    vortex::SqliteRepository repository(database_path);
    if (!repository.initialize()) return 2;
    vortex::DownloadQueueWorker download_worker(repository);
    download_worker.start();

#ifdef Q_OS_ANDROID
    AndroidLoopbackServer bridge_server(&repository);
    if (!bridge_server.start()) return 3;
#endif

    QQmlApplicationEngine engine;
#ifdef Q_OS_ANDROID
    engine.rootContext()->setContextProperty(QStringLiteral("bridgeServer"), &bridge_server);
#endif
    engine.loadFromModule("VortexDM", "Main");
    if (engine.rootObjects().isEmpty()) { download_worker.stop(); return 1; }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&download_worker] { download_worker.stop(); });
    return app.exec();
}
