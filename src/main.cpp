#include "app/Application.hpp"
#include "app/LanguageManager.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QtGlobal>

namespace {

bool hasListSensorsOption(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--list-sensors")) {
            return true;
        }
    }

    return false;
}

int listSensors(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenThermVane"));
    QCoreApplication::setApplicationName(QStringLiteral("ThermVane"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ThermVane"));
    parser.addHelpOption();
    QCommandLineOption listSensorsOption(QStringLiteral("list-sensors"),
                                         QStringLiteral("List detected temperature sensors and exit."));
    parser.addOption(listSensorsOption);
    parser.process(app);

    thermvane::Application application;

    QTextStream out(stdout);
    const auto sensors = application.sensors();
    for (const auto &sensor : sensors) {
        out << sensor.name << '\t'
            << (sensor.available ? QString::number(sensor.temperatureCelsius, 'f', 1) + QStringLiteral(" C")
                                 : QStringLiteral("--"))
            << '\t' << sensor.source << '\n';
    }

    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    if (hasListSensorsOption(argc, argv)) {
        return listSensors(argc, argv);
    }

    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "#55d6be");

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("OpenThermVane"));
    QGuiApplication::setApplicationName(QStringLiteral("ThermVane"));
    QQuickStyle::setStyle(QStringLiteral("Material"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ThermVane"));
    parser.addHelpOption();

    parser.process(app);

    thermvane::Application application;

    QQmlApplicationEngine engine;
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
