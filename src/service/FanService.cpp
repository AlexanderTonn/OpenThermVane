#include "service/FanService.hpp"

namespace thermvane {

FanService::FanService(QObject *parent)
    : QObject(parent)
{
}

void FanService::start()
{
    emit started();
}

} // namespace thermvane
