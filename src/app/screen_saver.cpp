#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {
namespace {

constexpr unsigned long kTriplePressWakeTimeoutMs = 3000;

}  // namespace

bool RecorderApp::anyInput(const InputEvent& event) const
{
    return event.g0 || event.left || event.right || event.up ||
           event.down || event.speedDown || event.speedUp ||
           event.confirm || event.back || event.fail || event.record ||
           event.deletePressed || event.settings || event.help;
}
bool RecorderApp::screenSaverAllowed() const
{
    return state_ == State::kBrowsing || state_ == State::kRecording ||
           state_ == State::kPlaying || state_ == State::kError;
}
std::uint8_t RecorderApp::screenSaverModeForState() const
{
    if (state_ == State::kRecording) {
        return settings_.recordingScreenMode;
    }
    if (state_ == State::kPlaying) {
        return settings_.playbackScreenMode;
    }
    return settings_.idleScreenMode;
}
void RecorderApp::serviceScreenSaver()
{
    if (screenSaverState_ != ScreenSaverState::kAwake &&
        wakeConfirmCount_ > 0 &&
        millis() - wakeConfirmLastMs_ > kTriplePressWakeTimeoutMs) {
        resetWakeConfirmation();
        if (screenSaverModeForState() == 2) {
            screenSaverState_ = ScreenSaverState::kOff;
            M5Cardputer.Display.setBrightness(0);
            M5Cardputer.Display.sleep();
            forceRedraw_ = false;
        } else {
            forceRedraw_ = true;
        }
    }

    if (!screenSaverAllowed()) {
        if (screenSaverState_ != ScreenSaverState::kAwake) {
            wakeScreen();
        }
        resetScreenSaverTimer();
        return;
    }

    if (screenSaverState_ != ScreenSaverState::kAwake ||
        screenSaverModeForState() == 0) {
        return;
    }

    const unsigned long delayMs =
        (state_ == State::kRecording || state_ == State::kPlaying)
            ? kActiveScreenSaverDelayMs
            : kIdleScreenSaverDelayMs;
    if (millis() - lastUserActivityMs_ >= delayMs) {
        enterScreenSaver(false);
    }
}
void RecorderApp::resetScreenSaverTimer()
{
    lastUserActivityMs_ = millis();
    screenSaverManual_ = false;
}
void RecorderApp::enterScreenSaver(bool manual)
{
    if (!screenSaverAllowed()) {
        return;
    }
    const std::uint8_t mode = screenSaverModeForState();
    if (mode == 0) {
        return;
    }

    screenSaverManual_ = manual;
    resetWakeConfirmation();
    if (mode == 2) {
        screenSaverState_ = ScreenSaverState::kOff;
        M5Cardputer.Display.setBrightness(0);
        M5Cardputer.Display.sleep();
        forceRedraw_ = false;
    } else {
        screenSaverState_ = ScreenSaverState::kDim;
        M5Cardputer.Display.wakeup();
        M5Cardputer.Display.setBrightness(kDimBrightness);
        forceRedraw_ = true;
    }
}
bool RecorderApp::handleScreenSaverWake(const InputEvent& event)
{
    if (!settings_.triplePressWake) {
        wakeScreen();
        return true;
    }

    const char key = event.primaryKey != '\0' ? event.primaryKey : '?';
    const unsigned long now = millis();
    if (wakeConfirmCount_ == 0 ||
        now - wakeConfirmLastMs_ > kTriplePressWakeTimeoutMs ||
        wakeConfirmKey_ != key) {
        wakeConfirmKey_ = key;
        wakeConfirmCount_ = 1;
    } else if (wakeConfirmCount_ < 3) {
        ++wakeConfirmCount_;
    }
    wakeConfirmLastMs_ = now;

    if (screenSaverState_ == ScreenSaverState::kOff) {
        screenSaverState_ = ScreenSaverState::kDim;
        M5Cardputer.Display.wakeup();
        M5Cardputer.Display.setBrightness(kDimBrightness);
    }

    if (wakeConfirmCount_ >= 3) {
        wakeScreen();
        return true;
    }

    forceRedraw_ = true;
    return true;
}
void RecorderApp::resetWakeConfirmation()
{
    wakeConfirmKey_ = '\0';
    wakeConfirmCount_ = 0;
    wakeConfirmLastMs_ = 0;
}
void RecorderApp::wakeScreen()
{
    M5Cardputer.Display.wakeup();
    applyBrightness();
    screenSaverState_ = ScreenSaverState::kAwake;
    screenSaverManual_ = false;
    resetWakeConfirmation();
    forceRedraw_ = true;
}
void RecorderApp::drawScreenSaver(unsigned long now)
{
    auto& display = recorderCanvas;
    constexpr std::uint16_t background = TFT_BLACK;
    constexpr std::uint16_t muted = 0x7BEF;
    constexpr std::uint16_t accent = 0x05FF;

    display.fillSprite(background);
    display.setTextDatum(top_left);
    display.setTextFont(1);
    display.setTextColor(muted, background);
    display.setCursor(8, 8);
    display.print("RECORDER");
    display.setTextDatum(top_right);
    if (battery_.valid) {
        display.drawString(String(battery_.levelPercent) + "%",
                           display.width() - 8, 8);
    } else {
        display.drawString("--%", display.width() - 8, 8);
    }
    display.setTextDatum(top_left);

    if (wakeConfirmCount_ > 0) {
        display.setTextFont(2);
        display.setTextDatum(middle_center);
        display.setTextColor(TFT_WHITE, background);
        display.drawString("Triple-Press Wake",
                           display.width() / 2, 44);
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.drawString("Press same key 3 times",
                           display.width() / 2, 68);
        display.setTextFont(2);
        display.setTextColor(accent, background);
        display.drawString(
            "Wake " + String(wakeConfirmCount_) + "/3",
            display.width() / 2, 91);
        display.setTextDatum(top_left);
    } else if (state_ == State::kRecording) {
        const std::uint32_t recordingBytes =
            recordingBytes_.load(std::memory_order_relaxed);
        display.fillCircle(20, 45, 7, TFT_RED);
        display.setTextFont(4);
        display.setTextDatum(middle_center);
        display.setTextColor(TFT_WHITE, background);
        display.drawString(formatTime(now - operationStartedMs_),
                           display.width() / 2, 58);
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.drawString(
            String(static_cast<unsigned long>(recordingBytes / 1024)) +
                " KB",
            display.width() / 2, 88);
        display.setTextDatum(top_left);
    } else if (state_ == State::kPlaying) {
        const unsigned long elapsed = playbackElapsedMs();
        const std::uint32_t percent =
            playbackDurationMs_ == 0
                ? 0
                : elapsed * 100 / playbackDurationMs_;
        display.setTextFont(4);
        display.setTextDatum(middle_center);
        display.setTextColor(TFT_WHITE, background);
        display.drawString(playbackPaused_ ? "PAUSED"
                                           : formatTime(elapsed),
                           display.width() / 2, 54);
        display.drawRoundRect(20, 80, display.width() - 40, 8, 3,
                              muted);
        display.fillRoundRect(
            22, 82, (display.width() - 44) * percent / 100, 4, 2,
            TFT_GREEN);
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.drawString(
            "VOL " +
                String(static_cast<unsigned int>(
                    playbackVolume_ * 100U / 255U)) +
                "% " + playbackSpeedText(playbackSpeedIndex_),
            display.width() / 2, 99);
        display.setTextDatum(top_left);
    } else {
        display.setTextFont(2);
        display.setTextDatum(middle_center);
        display.setTextColor(TFT_WHITE, background);
        display.drawString("Screen saver", display.width() / 2, 56);
        display.setTextFont(1);
        display.setTextColor(accent, background);
        display.drawString("Press any key", display.width() / 2, 82);
        display.setTextDatum(top_left);
    }

    if (screenSaverManual_) {
        display.setTextDatum(bottom_right);
        display.setTextFont(1);
        display.setTextColor(muted, background);
        display.drawString("MANUAL", display.width() - 8,
                           display.height() - 6);
        display.setTextDatum(top_left);
    }
    recorderCanvas.pushSprite(0, 0);
}

}  // namespace cardputer_recorder
