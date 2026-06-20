#pragma once

#include <Arduino.h>

namespace cardputer_recorder {

struct InputEvent {
    bool g0 = false;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool speedDown = false;
    bool speedUp = false;
    bool confirm = false;
    bool back = false;
    bool fail = false;
    bool record = false;
    bool deletePressed = false;
    bool settings = false;
    bool help = false;
    char primaryKey = '\0';
    String text;
};

class InputController {
public:
    InputEvent poll();

private:
    bool settingsHoldReported_ = false;
};

}  // namespace cardputer_recorder
