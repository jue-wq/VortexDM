#include <QGuiApplication>
#include <QIcon>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include "vortex/sqlite_repository.hpp"
#ifdef Q_OS_ANDROID
#include "android_loopback_server.hpp"
#endif

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/vortexdm-32.png")));

    const auto data_directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data_directory);
    vortex::SqliteRepository repository((data_directory + QStringLiteral("/vortexdm.sqlite3")).toStdString());
    if (!repository.initialize()) return 2;

#ifdef Q_OS_ANDROID
    AndroidLoopbackServer bridge_server(&repository);
    if (!bridge_server.start()) return 3;
#endif

    QQmlApplicationEngine engine;
#ifdef Q_OS_ANDROID
    engine.rootContext()->setContextProperty(QStringLiteral("bridgeServer"), &bridge_server);
#endif
    engine.loadFromModule("VortexDM", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
