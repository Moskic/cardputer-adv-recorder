#include "recorder/hardware/power_service.h"

#include <M5Cardputer.h>

namespace cardputer_recorder {

bool PowerService::begin()
{
    state_ = ServiceState::kReady;
    error_ = ErrorCode::kNone;
    return true;
}

void PowerService::update()
{
}

void PowerService::end()
{
    state_ = ServiceState::kStopped;
}

BatteryReading PowerService::readBattery()
{
    if (state_ != ServiceState::kReady) {
        begin();
    }
    BatteryReading reading;
    reading.levelPercent = M5Cardputer.Power.getBatteryLevel();
    reading.voltageMv = M5Cardputer.Power.getBatteryVoltage();
    reading.valid = reading.voltageMv >= 3000 && reading.voltageMv <= 4500;
    if (!reading.valid) {
        error_ = ErrorCode::kInvalidData;
    } else {
        error_ = ErrorCode::kNone;
    }
    return reading;
}

ServiceState PowerService::state() const
{
    return state_;
}

ErrorCode PowerService::lastError() const
{
    return error_;
}

}  // namespace cardputer_recorder
