#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>

class QGuiApplication;
class QQmlEngine;

namespace thermvane {

class LanguageManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)

public:
    LanguageManager(QGuiApplication &app, QQmlEngine &engine, QObject *parent = nullptr);

    QString language() const;
    QVariantList languages() const;
    Q_INVOKABLE QString translate(const QString &key, const QString &language = {}) const;
    Q_INVOKABLE void setLanguage(const QString &language);

signals:
    void languageChanged();

private:
    struct LanguageInfo
    {
        QString code;
        QString flag;
        QString name;
        QHash<QString, QString> strings;
    };

    bool loadLanguageFile(const QString &code);
    const LanguageInfo *languageInfo(const QString &code) const;

    QGuiApplication &m_app;
    QQmlEngine &m_engine;
    QHash<QString, LanguageInfo> m_languages;
    QVariantList m_languageList;
    QString m_language = QStringLiteral("en");
};

} // namespace thermvane
