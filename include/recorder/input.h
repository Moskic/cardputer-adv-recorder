#pragma once

namespace cardputer_recorder {

struct InputEvent {
    bool g0 = false;
    bool up = false;
    bool down = false;
    bool confirm = false;
    bool back = false;
    bool fail = false;
    bool record = false;
    bool deletePressed = false;
};

class InputController {
public:
    InputEvent poll();
};

}  // namespace cardputer_recorder
