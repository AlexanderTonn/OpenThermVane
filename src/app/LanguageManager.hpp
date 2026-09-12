#pragma once

#include <QObject>
#include <QTranslator>

class QGuiApplication;
class QQmlEngine;

namespace thermvane {

class LanguageManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    LanguageManager(QGuiApplication &app, QQmlEngine &engine, QObject *parent = nullptr);

    QString language() const;
    Q_INVOKABLE void setLanguage(const QString &language);

signals:
    void languageChanged();

private:
    QGuiApplication &m_app;
    QQmlEngine &m_engine;
    QTranslator m_translator;
    QString m_language = QStringLiteral("en");
};

} // namespace thermvane
