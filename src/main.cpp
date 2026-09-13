#include "app/Application.hpp"
#include "app/LanguageManager.hpp"
#include "hardware/macos/MacHardwareBackend.hpp"

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

bool hasOption(int argc, char *argv[], const QString &option)
{
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == option) {
            return true;
        }
    }

    return false;
}


int setFanSpeedCli(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenThermVane"));
    QCoreApplication::setApplicationName(QStringLiteral("ThermVane"));
    QCoreApplication::setApplicationVersion(QStringLiteral(THERMVANE_VERSION));
    qputenv("THERMVANE_NO_ADMIN_FALLBACK", "1");

    QString fanId;
    double percent = 0.0;
    bool percentOk = false;
    const QStringList arguments = app.arguments();
    for (int index = 1; index < arguments.size(); ++index) {
        if (arguments.at(index) == QStringLiteral("--set-fan-speed") && index + 2 < arguments.size()) {
            fanId = arguments.at(index + 1);
            percent = arguments.at(index + 2).toDouble(&percentOk);
            break;
        }
    }

    QTextStream err(stderr);
    if (fanId.isEmpty() || !percentOk) {
        err << "Usage: ThermVane --set-fan-speed <fan-id> <percent>\n";
        return 2;
    }

#if defined(Q_OS_MACOS)
    thermvane::MacHardwareBackend backend;
    if (backend.setFanSpeed(fanId, percent)) {
        return 0;
    }
#else
    Q_UNUSED(fanId)
    Q_UNUSED(percent)
#endif

    err << "Failed to set fan speed\n";
    return 1;
}

int listSensors(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenThermVane"));
    QCoreApplication::setApplicationName(QStringLiteral("ThermVane"));
    QCoreApplication::setApplicationVersion(QStringLiteral(THERMVANE_VERSION));

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
    if (hasOption(argc, argv, QStringLiteral("--set-fan-speed"))) {
        return setFanSpeedCli(argc, argv);
    }

    if (hasOption(argc, argv, QStringLiteral("--list-sensors"))) {
        return listSensors(argc, argv);
    }

    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "#55d6be");

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("OpenThermVane"));
    QGuiApplication::setApplicationName(QStringLiteral("ThermVane"));
    QGuiApplication::setApplicationVersion(QStringLiteral(THERMVANE_VERSION));
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
    engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"), QGuiApplication::applicationVersion());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("OpenThermVane"), QStringLiteral("Main"));
    return QGuiApplication::exec();
}
