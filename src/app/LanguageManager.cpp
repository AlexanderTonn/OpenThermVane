#include "app/LanguageManager.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSettings>

namespace thermvane {

namespace {

constexpr auto kLanguageResourcePrefix = ":/i18n/";
constexpr auto kLanguageSettingsKey = "ui/language";

} // namespace

LanguageManager::LanguageManager(QGuiApplication &app, QQmlEngine &engine, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_engine(engine)
{
    Q_UNUSED(m_app)

    loadLanguageFile(QStringLiteral("en"));
    loadLanguageFile(QStringLiteral("de"));

    const QString savedLanguage = QSettings().value(QString::fromLatin1(kLanguageSettingsKey), QStringLiteral("en"))
                                      .toString()
                                      .left(2)
                                      .toLower();
    if (m_languages.contains(savedLanguage)) {
        m_language = savedLanguage;
    }
}

QString LanguageManager::language() const
{
    return m_language;
}

QVariantList LanguageManager::languages() const
{
    return m_languageList;
}

QString LanguageManager::translate(const QString &key, const QString &language) const
{
    const QString requestedLanguage = language.isEmpty() ? m_language : language;
    if (const LanguageInfo *info = languageInfo(requestedLanguage)) {
        const auto it = info->strings.constFind(key);
        if (it != info->strings.cend()) {
            return it.value();
        }
    }

    if (requestedLanguage != QStringLiteral("en")) {
        if (const LanguageInfo *englishInfo = languageInfo(QStringLiteral("en"))) {
            const auto it = englishInfo->strings.constFind(key);
            if (it != englishInfo->strings.cend()) {
                return it.value();
            }
        }
    }

    return key;
}

void LanguageManager::setLanguage(const QString &language)
{
    const QString normalizedLanguage = language.left(2).toLower();
    if (!m_languages.contains(normalizedLanguage) || m_language == normalizedLanguage) {
        return;
    }

    m_language = normalizedLanguage;
    QSettings().setValue(QString::fromLatin1(kLanguageSettingsKey), m_language);
    m_engine.retranslate();
    emit languageChanged();
}

bool LanguageManager::loadLanguageFile(const QString &code)
{
    QFile file(QString::fromLatin1(kLanguageResourcePrefix) + code + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    LanguageInfo info;
    info.code = root.value(QStringLiteral("code")).toString(code).left(2).toLower();
    info.flag = root.value(QStringLiteral("flag")).toString();
    info.name = root.value(QStringLiteral("name")).toString(info.code);

    const QJsonObject strings = root.value(QStringLiteral("strings")).toObject();
    for (auto it = strings.constBegin(); it != strings.constEnd(); ++it) {
        info.strings.insert(it.key(), it.value().toString(it.key()));
    }

    if (info.code.isEmpty() || info.flag.isEmpty()) {
        return false;
    }

    const bool isNewLanguage = !m_languages.contains(info.code);
    m_languages.insert(info.code, info);

    if (isNewLanguage) {
        QVariantMap languageEntry;
        languageEntry.insert(QStringLiteral("code"), info.code);
        languageEntry.insert(QStringLiteral("flag"), info.flag);
        languageEntry.insert(QStringLiteral("name"), info.name);
        m_languageList.append(languageEntry);
    }

    return true;
}

const LanguageManager::LanguageInfo *LanguageManager::languageInfo(const QString &code) const
{
    const auto it = m_languages.constFind(code.left(2).toLower());
    return it == m_languages.cend() ? nullptr : &it.value();
}

} // namespace thermvane
