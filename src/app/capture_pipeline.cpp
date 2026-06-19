#include "recorder/recorder_app.h"

#include <cmath>

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

bool RecorderApp::primeCapture()
{
    while (captureActiveCount_ < kCaptureQueueDepth) {
        if (!queueCaptureBuffer()) {
            return false;
        }
    }
    return true;
}
bool RecorderApp::startCaptureWriter()
{
    stopCaptureWriter();
    captureWriterFailed_.store(false, std::memory_order_relaxed);
    captureWriterStopped_.store(false, std::memory_order_relaxed);
    captureWriterQueuePeak_ = 0;

    captureFreeQueue_ =
        xQueueCreate(kAudioBufferCount, sizeof(std::uint8_t));
    captureReadyQueue_ =
        xQueueCreate(kAudioBufferCount + 1, sizeof(std::uint8_t));
    if (captureFreeQueue_ == nullptr ||
        captureReadyQueue_ == nullptr) {
        stopCaptureWriter();
        return false;
    }

    for (std::uint8_t index = 0;
         index < kAudioBufferCount; ++index) {
        xQueueSend(captureFreeQueue_, &index, 0);
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        captureWriterTaskEntry, "recorder_sd", 4096, this, 1,
        &captureWriterTaskHandle_, 0);
    if (created != pdPASS) {
        captureWriterTaskHandle_ = nullptr;
        stopCaptureWriter();
        return false;
    }
    return true;
}
void RecorderApp::stopCaptureWriter()
{
    if (captureWriterTaskHandle_ != nullptr &&
        captureReadyQueue_ != nullptr) {
        const std::uint8_t marker = kCaptureStopMarker;
        xQueueSend(captureReadyQueue_, &marker, portMAX_DELAY);
        while (!captureWriterStopped_.load(std::memory_order_acquire)) {
            delay(1);
        }
        captureWriterTaskHandle_ = nullptr;
    }

    if (captureReadyQueue_ != nullptr) {
        vQueueDelete(captureReadyQueue_);
        captureReadyQueue_ = nullptr;
    }
    if (captureFreeQueue_ != nullptr) {
        vQueueDelete(captureFreeQueue_);
        captureFreeQueue_ = nullptr;
    }
    captureWriterStopped_.store(true, std::memory_order_release);
}
bool RecorderApp::queueCaptureBuffer()
{
    if (captureFreeQueue_ == nullptr ||
        captureActiveCount_ >= kCaptureQueueDepth) {
        return false;
    }

    std::uint8_t bufferIndex = 0;
    if (xQueueReceive(captureFreeQueue_, &bufferIndex, 0) != pdTRUE) {
        return false;
    }
    if (!audio_.record(audioBuffers_[bufferIndex],
                       kAudioSamplesPerBuffer)) {
        xQueueSend(captureFreeQueue_, &bufferIndex, 0);
        return false;
    }

    const std::size_t tail =
        (captureActiveHead_ + captureActiveCount_) %
        kCaptureQueueDepth;
    captureActive_[tail] = bufferIndex;
    ++captureActiveCount_;
    return true;
}
bool RecorderApp::serviceCompletedCapture(bool refill)
{
    const std::size_t queueLevel = audio_.captureQueueLevel();
    std::size_t completed =
        captureActiveCount_ > queueLevel
            ? captureActiveCount_ - queueLevel
            : 0;

    while (completed > 0) {
        const std::uint8_t completedIndex =
            captureActive_[captureActiveHead_];
        captureActiveHead_ =
            (captureActiveHead_ + 1) % kCaptureQueueDepth;
        --captureActiveCount_;
        --completed;

        // Refill M5Unified's newly available slot before handing the
        // completed block to the asynchronous SD writer.
        if (refill && !queueCaptureBuffer()) {
            xQueueSend(captureFreeQueue_, &completedIndex, 0);
            return false;
        }
        if (captureReadyQueue_ == nullptr ||
            xQueueSend(
                captureReadyQueue_, &completedIndex, 0) != pdTRUE) {
            xQueueSend(captureFreeQueue_, &completedIndex, 0);
            return false;
        }
        const std::size_t queued =
            uxQueueMessagesWaiting(captureReadyQueue_);
        if (queued > captureWriterQueuePeak_) {
            captureWriterQueuePeak_ = queued;
        }
    }
    return true;
}
void RecorderApp::processCaptureBuffer(std::uint8_t bufferIndex)
{
    std::int16_t* samples = audioBuffers_[bufferIndex];
    amplifyCapture(samples, kAudioSamplesPerBuffer);

    std::int64_t total = 0;
    for (std::size_t index = 0;
         index < kAudioSamplesPerBuffer; ++index) {
        total += samples[index];
    }
    const std::int32_t mean = static_cast<std::int32_t>(
        total / kAudioSamplesPerBuffer);
    std::uint64_t squaredTotal = 0;
    for (std::size_t index = 0;
         index < kAudioSamplesPerBuffer; ++index) {
        const std::int32_t centered =
            static_cast<std::int32_t>(samples[index]) - mean;
        squaredTotal +=
            static_cast<std::uint64_t>(centered) * centered;
    }
    const float rms = std::sqrt(
        static_cast<float>(squaredTotal) /
        kAudioSamplesPerBuffer);
    if (recordingNoiseFloor_ <= 1.0F) {
        recordingNoiseFloor_ = max(rms, 1.0F);
    } else if (rms < recordingNoiseFloor_ * 1.5F) {
        recordingNoiseFloor_ =
            recordingNoiseFloor_ * 0.98F + rms * 0.02F;
    }
    const float ratio =
        max(rms / max(recordingNoiseFloor_, 1.0F), 1.0F);
    const float levelDb =
        constrain(20.0F * std::log10(ratio), 0.0F, 24.0F);
    const float relativeLevel =
        6.0F + levelDb * 94.0F / 24.0F;
    const float dbfs = rms > 0.0F
                           ? 20.0F *
                                 std::log10(rms / 32768.0F)
                           : -96.0F;
    const float absoluteLevel =
        constrain((dbfs + 60.0F) * 100.0F / 48.0F,
                  0.0F, 100.0F);
    const std::uint8_t level = static_cast<std::uint8_t>(
        max(relativeLevel, absoluteLevel));
    const std::uint8_t previousLevel =
        recordingLevel_.load(std::memory_order_relaxed);
    recordingLevel_.store(
        static_cast<std::uint8_t>(
            previousLevel * 2 / 3 + level / 3),
        std::memory_order_relaxed);

    if (!writer_.writeSamples(samples, kAudioSamplesPerBuffer)) {
        Serial.printf(
            "[RECORDER] SD write failed errno=%d completed=%u/%u\n",
            writer_.lastWriteError(),
            static_cast<unsigned>(writer_.lastWriteCompleted()),
            static_cast<unsigned>(writer_.lastWriteRequested()));
        captureWriterFailed_.store(true, std::memory_order_release);
    } else {
        recordingBytes_.store(
            writer_.dataSize(), std::memory_order_relaxed);
    }
}
void RecorderApp::captureWriterTaskEntry(void* context)
{
    static_cast<RecorderApp*>(context)->captureWriterTask();
}
void RecorderApp::captureWriterTask()
{
    for (;;) {
        std::uint8_t bufferIndex = kCaptureStopMarker;
        if (xQueueReceive(
                captureReadyQueue_, &bufferIndex,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (bufferIndex == kCaptureStopMarker) {
            break;
        }
        if (!captureWriterFailed_.load(std::memory_order_acquire)) {
            processCaptureBuffer(bufferIndex);
        }
        xQueueSend(captureFreeQueue_, &bufferIndex, portMAX_DELAY);
    }

    captureWriterStopped_.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}
void RecorderApp::amplifyCapture(std::int16_t* samples,
                                 std::size_t count) const
{
    // ADV microphone PCM is conservative even at M5Unified's default input
    // gain. Add 6 dB before writing the WAV and softly compress peaks so the
    // saved file is useful on computers without post-processing.
    constexpr std::int32_t kGain = 2;
    constexpr std::int32_t kCompressionStart = 24500;
    for (std::size_t index = 0; index < count; ++index) {
        std::int32_t value =
            static_cast<std::int32_t>(samples[index]) * kGain;
        const bool negative = value < 0;
        std::int32_t magnitude = negative ? -value : value;
        if (magnitude > kCompressionStart) {
            magnitude =
                kCompressionStart +
                (magnitude - kCompressionStart) / 4;
        }
        magnitude = min(magnitude, 32767);
        samples[index] = static_cast<std::int16_t>(
            negative ? -magnitude : magnitude);
    }
}
void RecorderApp::amplifyPlayback(std::int16_t* samples,
                                  std::size_t count) const
{
    // M5Unified's 0..255 master volume is already at its maximum here.
    // Add up to 18 dB of PCM gain for the small ADV speaker, then compress
    // the last part of the range to avoid harsh integer clipping.
    const std::int32_t gain =
        256 + static_cast<std::int32_t>(playbackVolume_) * 7;
    constexpr std::int32_t kCompressionStart = 22000;
    for (std::size_t index = 0; index < count; ++index) {
        std::int32_t value =
            static_cast<std::int32_t>(samples[index]) * gain / 256;
        const bool negative = value < 0;
        std::int32_t magnitude = negative ? -value : value;
        if (magnitude > kCompressionStart) {
            magnitude =
                kCompressionStart +
                (magnitude - kCompressionStart) / 6;
        }
        magnitude = min(magnitude, 32767);
        samples[index] = static_cast<std::int16_t>(
            negative ? -magnitude : magnitude);
    }
}

}  // namespace cardputer_recorder
