#pragma once

#include <Arduino.h>
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
        kError,
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
    void stopPlayback();
    void deleteSelected();
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
    unsigned long playbackElapsedMs() const;
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

    State state_ = State::kBrowsing;
    std::vector<String> files_;
    int selected_ = 0;
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
    std::uint32_t playbackDurationMs_ = 0;
    std::uint32_t saveTotalBytes_ = 0;
    std::uint32_t saveCopiedBytes_ = 0;
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
