#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/vortexdm-32.png")));
    QQmlApplicationEngine engine;
    engine.loadFromModule("VortexDM", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
