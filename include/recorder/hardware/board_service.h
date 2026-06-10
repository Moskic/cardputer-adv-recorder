#pragma once

#include "recorder/hardware/service.h"

namespace cardputer_recorder {

class BoardService {
public:
    bool begin();
    void update();
    void end();

    ServiceState state() const;
    ErrorCode lastError() const;

private:
    ServiceState state_ = ServiceState::kStopped;
    ErrorCode error_ = ErrorCode::kNone;
};

}  // namespace cardputer_recorder
