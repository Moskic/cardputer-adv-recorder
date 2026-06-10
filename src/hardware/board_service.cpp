#include "recorder/hardware/board_service.h"

#include <M5Cardputer.h>

namespace cardputer_recorder {

bool BoardService::begin()
{
    state_ = ServiceState::kStarting;
    error_ = ErrorCode::kNone;

    auto config = M5.config();
    config.internal_mic = true;
    config.internal_spk = true;
    M5Cardputer.begin(config, true);

    if (M5.getBoard() != m5::board_t::board_M5CardputerADV) {
        state_ = ServiceState::kError;
        error_ = ErrorCode::kUnsupported;
        return false;
    }

    state_ = ServiceState::kReady;
    return true;
}

void BoardService::update()
{
    M5Cardputer.update();
}

void BoardService::end()
{
    state_ = ServiceState::kStopped;
}

ServiceState BoardService::state() const
{
    return state_;
}

ErrorCode BoardService::lastError() const
{
    return error_;
}

}  // namespace cardputer_recorder
