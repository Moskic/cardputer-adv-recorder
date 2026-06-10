#pragma once

#include "recorder/hardware/service.h"

namespace cardputer_recorder {

struct BatteryReading {
    int levelPercent = 0;
    int voltageMv = 0;
    bool valid = false;
};

class PowerService {
public:
    bool begin();
    void update();
    void end();

    BatteryReading readBattery();
    ServiceState state() const;
    ErrorCode lastError() const;

private:
    ServiceState state_ = ServiceState::kStopped;
    ErrorCode error_ = ErrorCode::kNone;
};

}  // namespace cardputer_recorder
