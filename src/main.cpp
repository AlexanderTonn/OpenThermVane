#include "app/Application.hpp"
#include "app/LanguageManager.hpp"
#include "hardware/macos/MacHardwareBackend.hpp"
#include "hardware/windows/WindowsHardwareBackend.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSystemTrayIcon>
#include <QQuickStyle>
#include <QTextStream>
#include <QWindow>
#include <QtGlobal>

#if defined(Q_OS_WIN) || defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

bool hasOption(int argc, char *argv[], const QString &option)
{
#if defined(Q_OS_WIN) || defined(_WIN32)
    const QString commandLine = QString::fromWCharArray(GetCommandLineW());
    if (commandLine.contains(option)) {
        return true;
    }
#endif

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
#elif defined(Q_OS_WIN) || defined(_WIN32)
    thermvane::WindowsHardwareBackend backend;
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

void showWindow(QWindow *window)
{
    if (!window) {
        return;
    }

    window->show();
    window->raise();
    window->requestActivate();
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

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("OpenThermVane"));
    QApplication::setApplicationName(QStringLiteral("ThermVane"));
    QApplication::setApplicationVersion(QStringLiteral(THERMVANE_VERSION));
    const QIcon appIcon(QStringLiteral(":/assets/icons/thermvane-icon.png"));
    QApplication::setWindowIcon(appIcon);
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
    engine.rootContext()->setContextProperty(QStringLiteral("I18n"), &languageManager);
    engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"), QApplication::applicationVersion());
    engine.rootContext()->setContextProperty(QStringLiteral("CMakeProjectVersion"), QStringLiteral(THERMVANE_VERSION));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("OpenThermVane"), QStringLiteral("Main"));

    QWindow *mainWindow = nullptr;
    if (!engine.rootObjects().isEmpty()) {
        mainWindow = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
        if (mainWindow) {
            mainWindow->setIcon(appIcon);
        }
    }

    QMenu trayMenu;
    QAction showAction(&trayMenu);
    QAction quitAction(&trayMenu);
    trayMenu.addAction(&showAction);
    trayMenu.addSeparator();
    trayMenu.addAction(&quitAction);
    const auto updateTrayTexts = [&languageManager, &showAction, &quitAction] {
        showAction.setText(languageManager.translate(QStringLiteral("Show ThermVane")));
        quitAction.setText(languageManager.translate(QStringLiteral("Quit")));
    };
    updateTrayTexts();
    QObject::connect(&languageManager, &thermvane::LanguageManager::languageChanged, &app, updateTrayTexts);

    QSystemTrayIcon trayIcon;
    if (QSystemTrayIcon::isSystemTrayAvailable() && !appIcon.isNull()) {
        trayIcon.setIcon(appIcon);
        trayIcon.setToolTip(QStringLiteral("ThermVane"));
        trayIcon.setContextMenu(&trayMenu);
        QObject::connect(&showAction, &QAction::triggered, &app, [mainWindow] {
            showWindow(mainWindow);
        });
        QObject::connect(&quitAction, &QAction::triggered, &app, &QCoreApplication::quit);
        QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &app,
                         [mainWindow](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                showWindow(mainWindow);
            }
        });
        trayIcon.show();
    }

    return QApplication::exec();
}
