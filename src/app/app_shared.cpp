#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

M5Canvas recorderCanvas(&M5Cardputer.Display);

String formatTime(unsigned long milliseconds)
{
    const unsigned long totalSeconds = milliseconds / 1000;
    const unsigned long minutes = totalSeconds / 60;
    const unsigned long seconds = totalSeconds % 60;
    char text[12];
    snprintf(text, sizeof(text), "%02lu:%02lu", minutes, seconds);
    return String(text);
}

String formatByteCount(std::uint64_t bytes)
{
    char text[16];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        const std::uint64_t tenths =
            bytes * 10ULL / (1024ULL * 1024ULL * 1024ULL);
        snprintf(text, sizeof(text), "%llu.%lluGB",
                 tenths / 10ULL, tenths % 10ULL);
    } else if (bytes >= 1024ULL * 1024ULL) {
        const std::uint64_t tenths =
            bytes * 10ULL / (1024ULL * 1024ULL);
        snprintf(text, sizeof(text), "%llu.%lluMB",
                 tenths / 10ULL, tenths % 10ULL);
    } else if (bytes >= 1024ULL) {
        snprintf(text, sizeof(text), "%lluKB", bytes / 1024ULL);
    } else {
        snprintf(text, sizeof(text), "%lluB", bytes);
    }
    return String(text);
}

const char* screenModeText(std::uint8_t mode)
{
    switch (mode) {
        case 0:
            return "OFF";
        case 1:
            return "Dimmed Standby";
        default:
            return "BLACK";
    }
}

const char* playbackSpeedText(std::uint8_t index)
{
    switch (index) {
        case 0:
            return "0.75x";
        case 2:
            return "1.25x";
        case 3:
            return "1.5x";
        case 4:
            return "2.0x";
        default:
            return "1.0x";
    }
}

const char* lowBatterySaveText(std::uint8_t percent)
{
    switch (percent) {
        case 1:
            return "1%";
        case 5:
            return "5%";
        case 10:
            return "10%";
        default:
            return "OFF";
    }
}

const char* seekStepText(std::uint8_t seconds)
{
    switch (seconds) {
        case 10:
            return "10 sec";
        case 20:
            return "20 sec";
        case 60:
            return "60 sec";
        default:
            return "5 sec";
    }
}

}  // namespace cardputer_recorder
