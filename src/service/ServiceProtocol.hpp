#pragma once

#include <QByteArray>
#include <QString>

namespace thermvane {

struct ServiceCommand
{
    QString name;
    QByteArray payload;
};

class ServiceProtocol
{
public:
    static QByteArray encode(const ServiceCommand &command);
    static ServiceCommand decode(const QByteArray &message);
};

} // namespace thermvane
