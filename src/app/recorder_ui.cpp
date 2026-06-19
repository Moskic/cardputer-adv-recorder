#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

void RecorderApp::draw()
{
    const unsigned long now = millis();
    const bool active =
        state_ == State::kRecording || state_ == State::kSaving ||
        state_ == State::kPlaying;
    if (!forceRedraw_ &&
        (!active || now - lastDrawMs_ < kActiveDrawIntervalMs)) {
        return;
    }
    forceRedraw_ = false;
    lastDrawMs_ = now;

    if (screenSaverState_ == ScreenSaverState::kOff) {
        return;
    }
    if (screenSaverState_ == ScreenSaverState::kDim) {
        drawScreenSaver(now);
        return;
    }

    auto& display = recorderCanvas;
    constexpr std::uint16_t background = 0x0841;
    constexpr std::uint16_t panel = 0x10C3;
    constexpr std::uint16_t selectedPanel = 0x1948;
    constexpr std::uint16_t muted = 0x8410;
    constexpr std::uint16_t accent = 0x05FF;

    display.fillSprite(background);
    display.fillRect(0, 0, display.width(), 24, panel);
    display.setTextFont(2);
    display.setTextColor(TFT_WHITE, panel);
    display.setCursor(8, 5);
    display.print("RECORDER");
    display.setTextFont(1);
    display.setTextColor(
        battery_.valid && battery_.levelPercent <= 15
            ? TFT_RED
            : TFT_LIGHTGREY,
        panel);
    display.setCursor(76, 8);
    if (battery_.valid) {
        display.printf("%d%%", battery_.levelPercent);
    } else {
        display.print("--%");
    }

    display.setTextDatum(top_right);
    display.setTextFont(1);
    display.setTextColor(accent, panel);
    const char* pageLabel =
        state_ == State::kRecording
            ? "RECORDING"
            : (state_ == State::kSaving
                   ? "SAVING"
                   : (state_ == State::kPlaying
                          ? "PLAYING"
                          : (state_ == State::kSettings
                                 ? (settingsPage_ ==
                                            SettingsPage::kScreenSaver
                                        ? "SCREEN"
                                        : "SETTINGS")
                                 : (state_ == State::kError ? "ERROR"
                                                           : "LIBRARY"))));
    display.drawString(pageLabel, display.width() - 8, 8);
    display.setTextDatum(top_left);

    if (state_ == State::kRecording) {
        const std::uint8_t recordingLevel =
            recordingLevel_.load(std::memory_order_relaxed);
        const std::uint32_t recordingBytes =
            recordingBytes_.load(std::memory_order_relaxed);
        display.fillCircle(16, 39, 5, TFT_RED);
        display.setTextFont(4);
        display.setTextDatum(middle_center);
        display.setTextColor(TFT_WHITE, background);
        display.drawString(
            formatTime(now - operationStartedMs_),
            display.width() / 2, 52);
        display.setTextDatum(top_left);
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.setCursor(8, 72);
        display.print(currentPath_);
        display.setTextDatum(top_right);
        display.drawString(
            String(static_cast<unsigned long>(
                       recordingBytes / 1024)) +
                " KB",
            display.width() - 8, 72);
        display.setTextDatum(top_left);
        display.drawRoundRect(8, 89, display.width() - 16, 12, 4,
                              muted);
        const int levelWidth =
            (display.width() - 20) * recordingLevel / 100;
        display.fillRoundRect(10, 91, levelWidth, 8, 3,
                              recordingLevel > 80 ? TFT_RED : accent);
        display.setTextColor(TFT_WHITE, background);
        display.setCursor(8, 112);
        display.print("ENTER / R");
        display.setTextDatum(top_right);
        display.setTextColor(muted, background);
        display.drawString("STOP & SAVE", display.width() - 8, 112);
        display.setTextDatum(top_left);
    } else if (state_ == State::kSaving) {
        const std::uint32_t percent =
            saveTotalBytes_ == 0
                ? 0
                : saveCopiedBytes_ * 100 / saveTotalBytes_;
        display.setTextFont(4);
        display.setTextDatum(middle_center);
        display.setTextColor(TFT_WHITE, background);
        display.drawString(String(percent) + "%",
                           display.width() / 2, 53);
        display.setTextDatum(top_left);
        display.drawRoundRect(12, 78, display.width() - 24, 14, 5,
                              muted);
        const int progress =
            (display.width() - 28) * percent / 100;
        display.fillRoundRect(14, 80, progress, 10, 4, accent);
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.setCursor(12, 101);
        display.printf("%lu / %lu KB",
                       static_cast<unsigned long>(
                           saveCopiedBytes_ / 1024),
                       static_cast<unsigned long>(
                           saveTotalBytes_ / 1024));
        display.setTextDatum(top_right);
        display.drawString("KEEP DEVICE ON",
                           display.width() - 12, 101);
        display.setTextDatum(top_left);
    } else if (state_ == State::kPlaying) {
        const unsigned long elapsed = playbackElapsedMs();
        const std::uint32_t percent =
            playbackDurationMs_ == 0
                ? 0
                : elapsed * 100 / playbackDurationMs_;
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.setCursor(8, 31);
        display.print(currentPath_);
        display.setTextFont(4);
        display.setTextDatum(middle_center);
        display.setTextColor(TFT_WHITE, background);
        display.drawString(formatTime(elapsed),
                           display.width() / 2, 59);
        display.setTextDatum(top_left);
        display.drawRoundRect(8, 81, display.width() - 16, 10, 4,
                              muted);
        display.fillRoundRect(
            10, 83, (display.width() - 20) * percent / 100, 6, 3,
            TFT_GREEN);
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.setCursor(8, 98);
        display.printf("%s left",
                       formatTime(playbackDurationMs_ - elapsed).c_str());
        display.setTextDatum(top_right);
        display.drawString(
            "VOL " +
                String(static_cast<unsigned int>(
                    playbackVolume_ * 100U / 255U)) +
                "%",
            display.width() - 8, 98);
        display.setTextDatum(top_left);
        display.setTextColor(TFT_WHITE, background);
        display.setCursor(8, 116);
        display.print("UP/DOWN volume");
        display.setTextDatum(top_right);
        display.drawString("ENTER stop", display.width() - 8, 116);
        display.setTextDatum(top_left);
    } else if (state_ == State::kSettings) {
        const bool screenSaverPage =
            settingsPage_ == SettingsPage::kScreenSaver;
        const char* mainLabels[kSettingsCount] = {
            "Brightness", "Screen Saver", "Idle Sleep WIP",
            "Playback Speed WIP", "VAD WIP"};
        const char* screenSaverLabels[kScreenSaverSettingsCount] = {
            "When Home", "While Recording", "While Playing"};
        const std::uint8_t settingCount =
            screenSaverPage ? kScreenSaverSettingsCount : kSettingsCount;
        int first = static_cast<int>(selectedSetting_) - 1;
        if (first < 0) {
            first = 0;
        }
        if (first + 4 > settingCount) {
            first = max(0, static_cast<int>(settingCount) - 4);
        }
        for (int row = 0; row < 4 && first + row < settingCount; ++row) {
            const int index = first + row;
            const int y = 31 + row * 19;
            const bool selected =
                index == static_cast<int>(selectedSetting_);
            if (selected) {
                display.fillRoundRect(5, y - 2, display.width() - 10,
                                      18, 4, selectedPanel);
            }
            display.setTextFont(1);
            display.setTextColor(selected ? TFT_WHITE : muted,
                                 selected ? selectedPanel : background);
            display.setCursor(10, y + 3);
            display.print(screenSaverPage ? screenSaverLabels[index]
                                          : mainLabels[index]);
            display.setTextDatum(middle_right);
            display.drawString(settingValueText(index),
                               display.width() - 10, y + 8);
            display.setTextDatum(top_left);
        }
        display.fillRect(0, 111, display.width(), 24, panel);
        display.setTextFont(1);
        display.setTextColor(accent, panel);
        display.setCursor(8, 120);
        display.print("LEFT/RIGHT value");
        display.setTextDatum(top_right);
        display.setTextColor(muted, panel);
        display.drawString(screenSaverPage ? "ESC BACK" : "ESC SAVE",
                           display.width() - 8, 120);
        display.setTextDatum(top_left);
    } else if (state_ == State::kError) {
        display.fillRoundRect(8, 34, display.width() - 16, 70, 6,
                              panel);
        display.setTextFont(2);
        display.setTextColor(TFT_RED, panel);
        display.setCursor(18, 44);
        display.print("Something went wrong");
        display.setTextFont(1);
        display.setTextColor(TFT_WHITE, panel);
        display.setCursor(18, 66);
        display.setTextWrap(true);
        display.print(message_);
        display.setTextWrap(false);
        display.setTextColor(accent, background);
        display.setCursor(8, 116);
        display.print("ENTER retry storage");
    } else {
        const String detail = selectedRecordingDetail();
        if (files_.empty()) {
            display.drawRoundRect(14, 33, display.width() - 28, 53, 8,
                                  muted);
            display.fillCircle(display.width() / 2, 51, 10, TFT_RED);
            display.setTextFont(2);
            display.setTextDatum(middle_center);
            display.setTextColor(TFT_WHITE, background);
            display.drawString("No recordings",
                               display.width() / 2, 75);
            display.setTextDatum(top_left);
        } else {
            int first = selected_ - 1;
            if (first < 0) {
                first = 0;
            }
            if (first + 3 > static_cast<int>(files_.size())) {
                first = max(0, static_cast<int>(files_.size()) - 3);
            }
            for (int row = 0; row < 3 &&
                              first + row <
                                  static_cast<int>(files_.size());
                 ++row) {
                const int index = first + row;
                const int y = 29 + row * 19;
                if (index == selected_) {
                    display.fillRoundRect(
                        5, y - 2, display.width() - 10, 18, 4,
                        selectedPanel);
                }
                display.setTextFont(2);
                display.setTextColor(
                    index == selected_ ? TFT_WHITE : muted,
                    index == selected_ ? selectedPanel : background);
                display.setCursor(10, y);
                display.print(files_[index]);
                display.setTextDatum(middle_right);
                display.setTextFont(1);
                display.drawString(
                    String(index + 1) + "/" +
                        String(files_.size()),
                    display.width() - 10, y + 7);
                display.setTextDatum(top_left);
            }
        }
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.setCursor(8, 94);
        display.print(detail);
        display.fillRect(0, 111, display.width(), 24, panel);
        display.setTextFont(1);
        display.setTextColor(TFT_RED, panel);
        display.setCursor(8, 120);
        display.print("R RECORD");
        display.setTextDatum(top_center);
        display.setTextColor(accent, panel);
        display.drawString("ENTER PLAY", display.width() / 2, 120);
        display.setTextDatum(top_right);
        display.setTextColor(muted, panel);
        display.drawString(files_.empty() ? "G0 SET" : "DEL",
                           display.width() - 8, 120);
        display.setTextDatum(top_left);
    }
    recorderCanvas.pushSprite(0, 0);
}

}  // namespace cardputer_recorder
