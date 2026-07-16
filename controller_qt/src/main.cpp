#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "core/EnvironmentChecker.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    // EnvironmentChecker (T27) — exposed as a global QML context property
    // "envChecker" so every page can read the readiness probes without
    // per-page instantiation. Declared before `engine` and on the stack so it
    // outlives the QML engine (destroyed after engine during stack unwind).
    // WelcomePage.qml calls envChecker.runChecks() on Component.onCompleted.
    EnvironmentChecker envChecker;
    engine.rootContext()->setContextProperty("envChecker", &envChecker);

    // Loads type "Main" from the QML module registered in CMakeLists.txt
    // (qt_add_qml_module, URI "DesktopPet").
    engine.loadFromModule("DesktopPet", "Main");

    return app.exec();
}
