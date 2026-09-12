#include "service/ServiceProtocol.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace thermvane {

QByteArray ServiceProtocol::encode(const ServiceCommand &command)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), command.name);
    object.insert(QStringLiteral("payload"), QString::fromUtf8(command.payload.toBase64()));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

ServiceCommand ServiceProtocol::decode(const QByteArray &message)
{
    const auto document = QJsonDocument::fromJson(message);
    const auto object = document.object();
    return {
        object.value(QStringLiteral("name")).toString(),
        QByteArray::fromBase64(object.value(QStringLiteral("payload")).toString().toUtf8()),
    };
}

} // namespace thermvane
