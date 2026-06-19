#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

void RecorderApp::openSettings()
{
    selectedSetting_ = 0;
    settingsPage_ = SettingsPage::kMain;
    state_ = State::kSettings;
    message_ = "Settings";
    forceRedraw_ = true;
}
void RecorderApp::closeSettings()
{
    saveSettings();
    state_ = State::kBrowsing;
    message_ = "Settings saved. Hold G0 to reopen.";
    forceRedraw_ = true;
}
void RecorderApp::handleSettingsInput(const InputEvent& event)
{
    if (event.settings || event.back) {
        if (settingsPage_ == SettingsPage::kScreenSaver) {
            settingsPage_ = SettingsPage::kMain;
            selectedSetting_ = 1;
            forceRedraw_ = true;
            return;
        }
        closeSettings();
        return;
    }
    const std::uint8_t settingCount =
        settingsPage_ == SettingsPage::kScreenSaver
            ? kScreenSaverSettingsCount
            : kSettingsCount;
    if (event.up) {
        selectedSetting_ =
            (selectedSetting_ + settingCount - 1) % settingCount;
        forceRedraw_ = true;
    } else if (event.down) {
        selectedSetting_ = (selectedSetting_ + 1) % settingCount;
        forceRedraw_ = true;
    } else if (event.right || event.confirm) {
        if (settingsPage_ == SettingsPage::kMain &&
            selectedSetting_ == 1) {
            settingsPage_ = SettingsPage::kScreenSaver;
            selectedSetting_ = 0;
            forceRedraw_ = true;
            return;
        }
        cycleSelectedSetting(1);
    } else if (event.left) {
        cycleSelectedSetting(-1);
    }
}
void RecorderApp::cycleSelectedSetting(int offset)
{
    if (settingsPage_ == SettingsPage::kScreenSaver) {
        std::uint8_t* value = nullptr;
        if (selectedSetting_ == 0) {
            value = &settings_.idleScreenMode;
        } else if (selectedSetting_ == 1) {
            value = &settings_.recordingScreenMode;
        } else if (selectedSetting_ == 2) {
            value = &settings_.playbackScreenMode;
        }
        if (value != nullptr) {
            *value = static_cast<std::uint8_t>(
                (static_cast<int>(*value) + 3 + offset) % 3);
            saveSettings();
            forceRedraw_ = true;
        }
        return;
    }

    switch (selectedSetting_) {
        case 0: {
            constexpr std::uint8_t values[] = {
                10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
            constexpr int count = sizeof(values) / sizeof(values[0]);
            int index = 0;
            for (int candidate = 0; candidate < count; ++candidate) {
                if (settings_.brightnessPercent == values[candidate]) {
                    index = candidate;
                    break;
                }
            }
            index = (index + count + offset) % count;
            settings_.brightnessPercent = values[index];
            applyBrightness();
            break;
        }
        case 2:
            settings_.idleSleepMode = static_cast<std::uint8_t>(
                (static_cast<int>(settings_.idleSleepMode) + 4 +
                 offset) %
                4);
            break;
        case 3:
            settings_.playbackSpeedIndex = static_cast<std::uint8_t>(
                (static_cast<int>(settings_.playbackSpeedIndex) + 5 +
                 offset) %
                5);
            break;
        case 4:
            settings_.vadEnabled = !settings_.vadEnabled;
            break;
        default:
            break;
    }
    saveSettings();
    forceRedraw_ = true;
}
void RecorderApp::loadSettings()
{
    preferences_.begin("recorder", false);
    settings_.brightnessPercent =
        preferences_.getUChar("bright", settings_.brightnessPercent);
    settings_.idleScreenMode =
        preferences_.getUChar("idle_scr", settings_.idleScreenMode);
    settings_.recordingScreenMode =
        preferences_.getUChar("rec_scr", settings_.recordingScreenMode);
    settings_.playbackScreenMode =
        preferences_.getUChar("play_scr", settings_.playbackScreenMode);
    settings_.idleSleepMode =
        preferences_.getUChar("sleep", settings_.idleSleepMode);
    settings_.playbackSpeedIndex =
        preferences_.getUChar("speed", settings_.playbackSpeedIndex);
    settings_.vadEnabled =
        preferences_.getBool("vad", settings_.vadEnabled);

    if (settings_.brightnessPercent < 10 ||
        settings_.brightnessPercent > 100) {
        settings_.brightnessPercent = 70;
    }
    if (settings_.idleScreenMode > 2) {
        settings_.idleScreenMode = 1;
    }
    if (settings_.recordingScreenMode > 2) {
        settings_.recordingScreenMode = 1;
    }
    if (settings_.playbackScreenMode > 2) {
        settings_.playbackScreenMode = 1;
    }
    if (settings_.idleSleepMode > 3) {
        settings_.idleSleepMode = 0;
    }
    if (settings_.playbackSpeedIndex > 4) {
        settings_.playbackSpeedIndex = 1;
    }
}
void RecorderApp::saveSettings()
{
    preferences_.putUChar("bright", settings_.brightnessPercent);
    preferences_.putUChar("idle_scr", settings_.idleScreenMode);
    preferences_.putUChar("rec_scr", settings_.recordingScreenMode);
    preferences_.putUChar("play_scr", settings_.playbackScreenMode);
    preferences_.putUChar("sleep", settings_.idleSleepMode);
    preferences_.putUChar("speed", settings_.playbackSpeedIndex);
    preferences_.putBool("vad", settings_.vadEnabled);
}
void RecorderApp::applyBrightness()
{
    const std::uint8_t brightness = static_cast<std::uint8_t>(
        max(1, static_cast<int>(settings_.brightnessPercent) * 255 / 100));
    M5Cardputer.Display.setBrightness(brightness);
}
String RecorderApp::settingValueText(std::uint8_t index) const
{
    if (settingsPage_ == SettingsPage::kScreenSaver) {
        switch (index) {
            case 0:
                return screenModeText(settings_.idleScreenMode);
            case 1:
                return screenModeText(settings_.recordingScreenMode);
            case 2:
                return screenModeText(settings_.playbackScreenMode);
            default:
                return "";
        }
    }

    switch (index) {
        case 0:
            return String(settings_.brightnessPercent) + "%";
        case 1:
            return ">";
        case 2:
            return idleSleepText(settings_.idleSleepMode);
        case 3:
            return playbackSpeedText(settings_.playbackSpeedIndex);
        case 4:
            return settings_.vadEnabled ? "ON" : "OFF";
        default:
            return "";
    }
}

}  // namespace cardputer_recorder
