#pragma once

#include <QDate>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>

class QSettings;

namespace thermvane {

class LoggingManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)

public:
    explicit LoggingManager(QObject *parent = nullptr);
    ~LoggingManager() override;

    bool enabled() const;
    void setEnabled(bool enabled);
    QString logDirectory() const;

signals:
    void enabledChanged();

private:
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &message);

    void handleMessage(QtMsgType type,
                       const QMessageLogContext &context,
                       const QString &message);
    void rotateIfNeeded();
    void rotateIfNeededLocked();
    bool openFileForDate(const QDate &date);
    void closeFile();
    void writeInternal(const QString &line);

    static LoggingManager *s_instance;

    QtMessageHandler m_previousHandler = nullptr;
    mutable QMutex m_mutex;
    QFile m_file;
    QDate m_fileDate;
    QString m_logDirectory;
    bool m_enabled = false;
    bool m_handlingMessage = false;
    QTimer m_rotationTimer;
};

} // namespace thermvane
