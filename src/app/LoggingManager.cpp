#include "app/LoggingManager.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QTextStream>
#include <QThread>

namespace thermvane {

LoggingManager *LoggingManager::s_instance = nullptr;

namespace {

constexpr qint64 kMaxLogFileBytes = 16 * 1024 * 1024;
constexpr QFileDevice::Permissions kPrivateFilePermissions =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr QFileDevice::Permissions kPrivateDirectoryPermissions =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;

QSettings settings()
{
#if defined(Q_OS_MACOS)
    return QSettings(QDir::homePath()
                         + QStringLiteral("/Library/Preferences/com.openthermvane.ThermVane.plist"),
                     QSettings::NativeFormat);
#else
    return QSettings();
#endif
}

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARNING");
    case QtCriticalMsg: return QStringLiteral("CRITICAL");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

QString escapeMessage(QString message)
{
    message.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    message.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return message;
}

} // namespace

LoggingManager::LoggingManager(QObject *parent)
    : QObject(parent)
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.thermvane");
    }
    m_logDirectory = QDir(directory).filePath(QStringLiteral("logs"));

    QSettings storedSettings = settings();
    m_enabled = storedSettings.value(QStringLiteral("logging/enabled"), false).toBool();

    s_instance = this;
    m_previousHandler = qInstallMessageHandler(&LoggingManager::messageHandler);

    connect(&m_rotationTimer, &QTimer::timeout, this, &LoggingManager::rotateIfNeeded);
    m_rotationTimer.setInterval(60 * 1000);
    m_rotationTimer.start();

    if (m_enabled) {
        const QDate today = QDate::currentDate();
        if (openFileForDate(today)) {
            writeInternal(QStringLiteral("[%1] [INFO] logging enabled (application startup)")
                              .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
        }
    }
}

LoggingManager::~LoggingManager()
{
    m_rotationTimer.stop();
    {
        QMutexLocker locker(&m_mutex);
        if (m_enabled && m_file.isOpen()) {
            writeInternal(QStringLiteral("[%1] [INFO] application shutdown")
                              .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
        }
        closeFile();
    }

    if (s_instance == this) {
        qInstallMessageHandler(m_previousHandler);
        s_instance = nullptr;
    }
}

bool LoggingManager::enabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_enabled;
}

void LoggingManager::setEnabled(bool enabledValue)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_enabled == enabledValue) {
            return;
        }

        m_enabled = enabledValue;
        QSettings storedSettings = settings();
        storedSettings.setValue(QStringLiteral("logging/enabled"), m_enabled);
        changed = true;

        if (m_enabled) {
            if (openFileForDate(QDate::currentDate())) {
                writeInternal(QStringLiteral("[%1] [INFO] logging enabled")
                                  .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
            }
        } else {
            if (m_file.isOpen()) {
                writeInternal(QStringLiteral("[%1] [INFO] logging disabled")
                                  .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
            }
            closeFile();
        }
    }

    if (changed) {
        emit enabledChanged();
    }
}

QString LoggingManager::logDirectory() const
{
    return m_logDirectory;
}

void LoggingManager::messageHandler(QtMsgType type,
                                    const QMessageLogContext &context,
                                    const QString &message)
{
    LoggingManager *manager = s_instance;
    if (manager) {
        manager->handleMessage(type, context, message);
    }

    if (manager && manager->m_previousHandler) {
        manager->m_previousHandler(type, context, message);
    }
}

void LoggingManager::handleMessage(QtMsgType type,
                                   const QMessageLogContext &context,
                                   const QString &message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_enabled || m_handlingMessage) {
        return;
    }

    m_handlingMessage = true;
    rotateIfNeededLocked();
    if (m_file.isOpen()) {
        const QString category = context.category ? QString::fromUtf8(context.category)
                                                  : QStringLiteral("default");
        QString location;
        if (context.file && *context.file) {
            location = QStringLiteral(" (%1:%2 %3)")
                           .arg(QString::fromUtf8(context.file))
                           .arg(context.line)
                           .arg(context.function ? QString::fromUtf8(context.function)
                                                 : QStringLiteral("?"));
        }
        const QString line = QStringLiteral("[%1] [%2] [thread=%3] [%4] %5%6")
                                 .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                                 .arg(levelName(type))
                                 .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                                 .arg(category)
                                 .arg(escapeMessage(message))
                                 .arg(location);
        writeInternal(line);
    }
    m_handlingMessage = false;
}

void LoggingManager::rotateIfNeeded()
{
    QMutexLocker locker(&m_mutex);
    rotateIfNeededLocked();
}

void LoggingManager::rotateIfNeededLocked()
{
    if (!m_enabled) {
        return;
    }

    const QDate today = QDate::currentDate();
    if (!m_file.isOpen() || m_fileDate != today) {
        closeFile();
        openFileForDate(today);
    }
}

bool LoggingManager::openFileForDate(const QDate &date)
{
    if (date.isValid() && m_file.isOpen() && m_fileDate == date) {
        return true;
    }

    if (!QDir().mkpath(m_logDirectory)) {
        return false;
    }
    QFile::setPermissions(m_logDirectory, kPrivateDirectoryPermissions);

    const QString filename = QDir(m_logDirectory).filePath(
        QStringLiteral("thermvane-%1.log").arg(date.toString(QStringLiteral("yyyy-MM-dd"))));
    m_file.setFileName(filename);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_fileDate = {};
        return false;
    }

    // Do not expose sensor names, paths, or fan state to other local users.
    m_file.setPermissions(kPrivateFilePermissions);

    m_fileDate = date;
    return true;
}

void LoggingManager::closeFile()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    m_fileDate = {};
}

void LoggingManager::writeInternal(const QString &line)
{
    if (!m_file.isOpen()) {
        return;
    }

    // A one-line-per-scan debug log can otherwise grow without bound during a
    // multi-day session. Keep the current day's file bounded; the daily
    // rotation policy remains intact.
    if (m_file.size() >= kMaxLogFileBytes) {
        const QString fileName = m_file.fileName();
        m_file.close();
        m_file.setFileName(fileName);
        if (m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            m_file.setPermissions(kPrivateFilePermissions);
        }
    }

    QTextStream stream(&m_file);
    stream << line << '\n';
    stream.flush();
}

} // namespace thermvane
