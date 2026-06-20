#include "recorder/input.h"

#include <M5Cardputer.h>

namespace cardputer_recorder {

InputEvent InputController::poll()
{
    InputEvent event;
    event.g0 = M5Cardputer.BtnA.wasReleased() &&
               !M5Cardputer.BtnA.wasReleaseFor(900);
    if (event.g0) {
        event.primaryKey = 'G';
    }
    if (M5Cardputer.BtnA.pressedFor(900)) {
        if (!settingsHoldReported_) {
            event.settings = true;
            settingsHoldReported_ = true;
            event.primaryKey = 'G';
        }
    } else if (!M5Cardputer.BtnA.isPressed()) {
        settingsHoldReported_ = false;
    }
    if (!M5Cardputer.Keyboard.isChange() ||
        !M5Cardputer.Keyboard.isPressed()) {
        return event;
    }

    const auto& state = M5Cardputer.Keyboard.keysState();
    event.confirm = state.enter;
    event.deletePressed = state.del;
    if (event.confirm) {
        event.primaryKey = '\n';
    } else if (event.deletePressed) {
        event.primaryKey = '\b';
    }

    for (const char character : state.word) {
        if (event.primaryKey == '\0') {
            event.primaryKey = character;
        }
        event.text += character;
        switch (character) {
            case ',':
                event.left = true;
                break;
            case ';':
                event.up = true;
                break;
            case '.':
                event.down = true;
                break;
            case '/':
                event.right = true;
                break;
            case '[':
                event.speedDown = true;
                break;
            case ']':
                event.speedUp = true;
                break;
            case '`':
                event.back = true;
                break;
            case 'f':
            case 'F':
                event.fail = true;
                break;
            case 'h':
            case 'H':
                event.help = true;
                break;
            case 'r':
            case 'R':
                event.record = true;
                break;
            default:
                break;
        }
    }

    return event;
}

}  // namespace cardputer_recorder
