#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <vector>

#include "recorder/hardware/audio_service.h"
#include "recorder/hardware/board_service.h"
#include "recorder/hardware/power_service.h"
#include "recorder/hardware/storage_service.h"
#include "recorder/input.h"
#include "recorder/media/wav_file.h"

namespace cardputer_recorder {

class RecorderApp {
public:
    void begin();
    void update();

private:
    enum class State : std::uint8_t {
        kBrowsing,
        kRecording,
        kSaving,
        kPlaying,
        kSettings,
        kRename,
        kHelp,
        kError,
    };

    struct Settings {
        std::uint8_t brightnessPercent = 70;
        std::uint8_t idleScreenMode = 1;
        std::uint8_t recordingScreenMode = 1;
        std::uint8_t playbackScreenMode = 1;
        std::uint8_t lowBatterySavePercent = 10;
        std::uint8_t seekStepSeconds = 5;
        bool vadEnabled = false;
        bool triplePressWake = false;
    };

    enum class ScreenSaverState : std::uint8_t {
        kAwake,
        kDim,
        kOff,
    };

    enum class SettingsPage : std::uint8_t {
        kMain,
        kScreenSaver,
    };

    void handleInput(const InputEvent& event);
    bool startRecording();
    void serviceRecording();
    bool drainRecording();
    void stopRecording(bool keepFile);
    bool beginSaving();
    void serviceSaving();
    void finishSaving(bool success, const String& detail);
    bool startPlayback();
    void servicePlayback();
    void togglePlaybackPause();
    bool seekPlayback(int seconds);
    bool changePlaybackSpeed(int offset);
    void stopPlayback();
    void openSettings();
    void closeSettings();
    void handleSettingsInput(const InputEvent& event);
    void openHelp();
    void handleHelpInput(const InputEvent& event);
    void beginRenameSelected();
    void handleRenameInput(const InputEvent& event);
    void commitRename();
    void cycleSelectedSetting(int offset);
    void resetSettingsToDefault();
    void loadSettings();
    void saveSettings();
    void applyBrightness();
    String settingValueText(std::uint8_t index) const;
    bool anyInput(const InputEvent& event) const;
    bool screenSaverAllowed() const;
    std::uint8_t screenSaverModeForState() const;
    void serviceScreenSaver();
    void resetScreenSaverTimer();
    void enterScreenSaver(bool manual);
    bool handleScreenSaverWake(const InputEvent& event);
    void resetWakeConfirmation();
    void wakeScreen();
    void drawScreenSaver(unsigned long now);
    void deleteSelected();
    void toggleLockSelected();
    bool isLocked(const String& filename) const;
    void loadLocks();
    bool saveLocks();
    void scanFiles();
    bool chooseRecordingPath(char* path, std::size_t capacity);
    bool primeCapture();
    bool startCaptureWriter();
    void stopCaptureWriter();
    bool queueCaptureBuffer();
    bool serviceCompletedCapture(bool refill);
    void processCaptureBuffer(std::uint8_t bufferIndex);
    static void captureWriterTaskEntry(void* context);
    void captureWriterTask();
    void amplifyCapture(std::int16_t* samples, std::size_t count) const;
    void amplifyPlayback(std::int16_t* samples, std::size_t count) const;
    void adjustVolume(int offset);
    void updateBattery(bool force = false);
    bool shouldAutoSaveForLowBattery() const;
    unsigned long playbackElapsedMs() const;
    std::uint32_t playbackBytesPerSecond() const;
    std::uint32_t playbackOutputSampleRate() const;
    String storageUsageText() const;
    String selectedRecordingDetail();
    void setError(const String& message);
    void draw();

    // M5Unified exposes two microphone queue slots. Extra buffers absorb SD
    // latency while a dedicated task writes completed blocks.
    static constexpr std::size_t kCaptureQueueDepth = 2;
    static constexpr std::size_t kAudioBufferCount = 10;
    static constexpr std::size_t kAudioSamplesPerBuffer = 4096;
    static constexpr std::uint8_t kCaptureStopMarker = 0xFF;

    BoardService board_;
    StorageService storage_;
    AudioService audio_;
    PowerService power_;
    InputController input_;
    WavWriter writer_;
    WavReader reader_;
    Preferences preferences_;

    State state_ = State::kBrowsing;
    Settings settings_;
    std::uint8_t selectedSetting_ = 0;
    std::uint8_t helpPage_ = 0;
    bool resetSettingsConfirm_ = false;
    SettingsPage settingsPage_ = SettingsPage::kMain;
    ScreenSaverState screenSaverState_ = ScreenSaverState::kAwake;
    bool screenSaverManual_ = false;
    char wakeConfirmKey_ = '\0';
    std::uint8_t wakeConfirmCount_ = 0;
    unsigned long wakeConfirmLastMs_ = 0;
    unsigned long lastUserActivityMs_ = 0;
    std::vector<String> files_;
    std::vector<String> lockedFiles_;
    int selected_ = 0;
    bool deleteConfirm_ = false;
    String deleteConfirmName_;
    String renameOriginalName_;
    String renameText_;
    std::int16_t
        audioBuffers_[kAudioBufferCount][kAudioSamplesPerBuffer] = {};
    std::uint8_t captureActive_[kCaptureQueueDepth] = {};
    std::size_t captureActiveHead_ = 0;
    std::size_t captureActiveCount_ = 0;
    bool captureQueueActive_ = false;
    QueueHandle_t captureFreeQueue_ = nullptr;
    QueueHandle_t captureReadyQueue_ = nullptr;
    TaskHandle_t captureWriterTaskHandle_ = nullptr;
    std::atomic<bool> captureWriterFailed_{false};
    std::atomic<bool> captureWriterStopped_{true};
    std::size_t captureWriterQueuePeak_ = 0;
    std::uint32_t captureStarvationCount_ = 0;
    std::atomic<std::uint8_t> recordingLevel_{0};
    std::atomic<std::uint32_t> recordingBytes_{0};
    float recordingNoiseFloor_ = 0.0F;
    std::uint8_t playbackBufferIndex_ = 0;
    std::uint8_t playbackVolume_ = 255;
    std::uint8_t playbackSpeedIndex_ = 1;
    std::uint32_t playbackDurationMs_ = 0;
    std::uint32_t playbackBaseElapsedMs_ = 0;
    bool playbackPaused_ = false;
    std::uint32_t saveTotalBytes_ = 0;
    std::uint32_t saveCopiedBytes_ = 0;
    bool lowBatteryAutoSave_ = false;
    unsigned long saveSettledAtMs_ = 0;
    bool saveAwaitingSettle_ = false;
    String currentPath_;
    String message_;
    unsigned long operationStartedMs_ = 0;
    unsigned long lastBatteryReadMs_ = 0;
    unsigned long lastDrawMs_ = 0;
    BatteryReading battery_;
    bool forceRedraw_ = true;
};

}  // namespace cardputer_recorder
