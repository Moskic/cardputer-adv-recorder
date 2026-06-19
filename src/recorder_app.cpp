#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

void RecorderApp::begin()
{
    Serial.begin(115200);
    if (!board_.begin()) {
        setError("Cardputer ADV required.");
        return;
    }

    audio_.begin();
    power_.begin();
    loadSettings();
    updateBattery(true);
    auto& display = M5Cardputer.Display;
    display.setRotation(1);
    applyBrightness();
    display.fillScreen(TFT_BLACK);
    display.setTextWrap(false);
    recorderCanvas.setColorDepth(8);
    recorderCanvas.createSprite(display.width(), display.height());
    recorderCanvas.setTextWrap(false);
    resetScreenSaverTimer();

    if (!storage_.begin()) {
        setError("Insert a writable microSD card.");
    } else {
        scanFiles();
        message_ = "Hold G0 for settings.";
    }
    draw();
}

void RecorderApp::update()
{
    board_.update();
    storage_.update();
    audio_.update();
    power_.update();
    updateBattery();

    const InputEvent event = input_.poll();
    handleInput(event);
    serviceScreenSaver();

    if (state_ == State::kRecording) {
        serviceRecording();
    } else if (state_ == State::kSaving) {
        serviceSaving();
    } else if (state_ == State::kPlaying) {
        servicePlayback();
    }

    draw();
    delay(1);
}

void RecorderApp::handleInput(const InputEvent& event)
{
    const bool hasInput = anyInput(event);
    if (hasInput) {
        if (screenSaverState_ != ScreenSaverState::kAwake) {
            wakeScreen();
            resetScreenSaverTimer();
            return;
        }
        resetScreenSaverTimer();
    }

    if (state_ == State::kRecording) {
        if (event.g0) {
            enterScreenSaver(true);
            return;
        }
        if (millis() - operationStartedMs_ >=
                kRecordingStopGuardMs &&
            (event.confirm || event.back || event.record)) {
            stopRecording(true);
        }
        return;
    }
    if (state_ == State::kSaving) {
        return;
    }
    if (state_ == State::kSettings) {
        handleSettingsInput(event);
        return;
    }
    if (state_ == State::kPlaying) {
        if (event.g0) {
            enterScreenSaver(true);
            return;
        }
        if (event.up) {
            adjustVolume(16);
        } else if (event.down) {
            adjustVolume(-16);
        }
        if (event.confirm || event.back) {
            stopPlayback();
        }
        return;
    }
    if (state_ == State::kError) {
        if (event.confirm) {
            if (storage_.remount()) {
                state_ = State::kBrowsing;
                scanFiles();
                message_ = "Storage ready.";
                forceRedraw_ = true;
            }
        }
        return;
    }

    if (event.g0) {
        enterScreenSaver(true);
        return;
    }
    if (event.settings) {
        openSettings();
        return;
    }
    if (event.up && !files_.empty()) {
        selected_ =
            (selected_ + static_cast<int>(files_.size()) - 1) %
            static_cast<int>(files_.size());
        forceRedraw_ = true;
    } else if (event.down && !files_.empty()) {
        selected_ = (selected_ + 1) % static_cast<int>(files_.size());
        forceRedraw_ = true;
    } else if (event.record) {
        startRecording();
    } else if (event.confirm && !files_.empty()) {
        startPlayback();
    } else if (event.deletePressed && !files_.empty()) {
        deleteSelected();
    }
}

void RecorderApp::updateBattery(bool force)
{
    const unsigned long now = millis();
    if (!force && now - lastBatteryReadMs_ < 5000) {
        return;
    }
    lastBatteryReadMs_ = now;
    battery_ = power_.readBattery();
    forceRedraw_ = true;
}

void RecorderApp::setError(const String& message)
{
    if (state_ == State::kRecording) {
        stopRecording(false);
    } else if (state_ == State::kPlaying) {
        stopPlayback();
    }
    state_ = State::kError;
    message_ = message;
    forceRedraw_ = true;
    Serial.println("[RECORDER] " + message);
}

}  // namespace cardputer_recorder
