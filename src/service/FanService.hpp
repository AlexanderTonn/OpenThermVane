#pragma once

#include <QObject>

namespace thermvane {

class FanService final : public QObject
{
    Q_OBJECT

public:
    explicit FanService(QObject *parent = nullptr);

    Q_INVOKABLE void start();

signals:
    void started();
};

} // namespace thermvane
