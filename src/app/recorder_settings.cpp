#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

void RecorderApp::openSettings()
{
    selectedSetting_ = 0;
    settingsPage_ = SettingsPage::kMain;
    resetSettingsConfirm_ = false;
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
        if (resetSettingsConfirm_) {
            resetSettingsConfirm_ = false;
            message_ = "Reset canceled.";
            forceRedraw_ = true;
            return;
        }
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
        resetSettingsConfirm_ = false;
        selectedSetting_ =
            (selectedSetting_ + settingCount - 1) % settingCount;
        forceRedraw_ = true;
    } else if (event.down) {
        resetSettingsConfirm_ = false;
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
        if (resetSettingsConfirm_) {
            resetSettingsConfirm_ = false;
            message_ = "Reset canceled.";
            forceRedraw_ = true;
            return;
        }
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
        } else if (selectedSetting_ == 3) {
            settings_.triplePressWake = !settings_.triplePressWake;
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
        {
            constexpr std::uint8_t values[] = {0, 1, 5, 10};
            constexpr int count = sizeof(values) / sizeof(values[0]);
            int index = 3;
            for (int candidate = 0; candidate < count; ++candidate) {
                if (settings_.lowBatterySavePercent ==
                    values[candidate]) {
                    index = candidate;
                    break;
                }
            }
            index = (index + count + offset) % count;
            settings_.lowBatterySavePercent = values[index];
            break;
        }
        case 3:
        {
            constexpr std::uint8_t values[] = {5, 10, 20, 60};
            constexpr int count = sizeof(values) / sizeof(values[0]);
            int index = 0;
            for (int candidate = 0; candidate < count; ++candidate) {
                if (settings_.seekStepSeconds == values[candidate]) {
                    index = candidate;
                    break;
                }
            }
            index = (index + count + offset) % count;
            settings_.seekStepSeconds = values[index];
            break;
        }
        case 4:
            if (resetSettingsConfirm_) {
                resetSettingsToDefault();
            } else if (offset > 0) {
                resetSettingsConfirm_ = true;
                message_ = "Press Enter again to reset.";
            }
            break;
        case 5:
            break;
        default:
            break;
    }
    saveSettings();
    forceRedraw_ = true;
}
void RecorderApp::resetSettingsToDefault()
{
    settings_ = Settings{};
    resetSettingsConfirm_ = false;
    saveSettings();
    applyBrightness();
    resetScreenSaverTimer();
    message_ = "Settings reset.";
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
    settings_.lowBatterySavePercent =
        preferences_.getUChar("low_save", settings_.lowBatterySavePercent);
    settings_.seekStepSeconds =
        preferences_.getUChar("seek_step", settings_.seekStepSeconds);
    settings_.vadEnabled =
        preferences_.getBool("vad", settings_.vadEnabled);
    settings_.triplePressWake =
        preferences_.getBool("triple_wake", settings_.triplePressWake);

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
    if (settings_.lowBatterySavePercent != 0 &&
        settings_.lowBatterySavePercent != 1 &&
        settings_.lowBatterySavePercent != 5 &&
        settings_.lowBatterySavePercent != 10) {
        settings_.lowBatterySavePercent = 10;
    }
    if (settings_.seekStepSeconds != 5 &&
        settings_.seekStepSeconds != 10 &&
        settings_.seekStepSeconds != 20 &&
        settings_.seekStepSeconds != 60) {
        settings_.seekStepSeconds = 5;
    }
}
void RecorderApp::saveSettings()
{
    preferences_.putUChar("bright", settings_.brightnessPercent);
    preferences_.putUChar("idle_scr", settings_.idleScreenMode);
    preferences_.putUChar("rec_scr", settings_.recordingScreenMode);
    preferences_.putUChar("play_scr", settings_.playbackScreenMode);
    preferences_.putUChar("low_save", settings_.lowBatterySavePercent);
    preferences_.putUChar("seek_step", settings_.seekStepSeconds);
    preferences_.putBool("vad", settings_.vadEnabled);
    preferences_.putBool("triple_wake", settings_.triplePressWake);
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
            case 3:
                return settings_.triplePressWake ? "ON" : "OFF";
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
            return lowBatterySaveText(settings_.lowBatterySavePercent);
        case 3:
            return seekStepText(settings_.seekStepSeconds);
        case 4:
            return resetSettingsConfirm_ ? "CONFIRM?" : "";
        case 5:
            return kAppVersion;
        default:
            return "";
    }
}

}  // namespace cardputer_recorder
