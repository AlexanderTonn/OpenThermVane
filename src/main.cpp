#include "app/Application.hpp"
#include "app/LanguageManager.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtGlobal>

int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "#55d6be");

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("OpenThermVane"));
    QGuiApplication::setApplicationName(QStringLiteral("ThermVane"));
    QQuickStyle::setStyle(QStringLiteral("Material"));

    QQmlApplicationEngine engine;
    thermvane::Application application;
    thermvane::LanguageManager languageManager(app, engine);

    engine.rootContext()->setContextProperty(QStringLiteral("FanModel"), application.fanModel());
    engine.rootContext()->setContextProperty(QStringLiteral("SensorModel"), application.sensorModel());
    engine.rootContext()->setContextProperty(QStringLiteral("FanCurveModel"), application.fanCurveModel());
    engine.rootContext()->setContextProperty(QStringLiteral("FanController"), application.fanController());
    engine.rootContext()->setContextProperty(QStringLiteral("LanguageManager"), &languageManager);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("OpenThermVane"), QStringLiteral("Main"));
    return QGuiApplication::exec();
}
