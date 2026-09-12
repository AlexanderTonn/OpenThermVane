#include "app/LanguageManager.hpp"

#include <QGuiApplication>
#include <QQmlEngine>

namespace thermvane {

LanguageManager::LanguageManager(QGuiApplication &app, QQmlEngine &engine, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_engine(engine)
{
}

QString LanguageManager::language() const
{
    return m_language;
}

void LanguageManager::setLanguage(const QString &language)
{
    if (m_language == language) {
        return;
    }

    m_app.removeTranslator(&m_translator);

    if (language == QStringLiteral("de")) {
        if (m_translator.load(QStringLiteral(":/i18n/thermvane_de.qm"))) {
            m_app.installTranslator(&m_translator);
        }
    }

    m_language = language;
    m_engine.retranslate();
    emit languageChanged();
}

} // namespace thermvane
