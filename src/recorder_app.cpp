#include "recorder/recorder_app.h"

#include <M5Cardputer.h>
#include <cmath>

#include "recorder/media/file_naming.h"

namespace cardputer_recorder {
namespace {

constexpr WavSpec kRecordingSpec{16000, 1, 16};
constexpr AudioFormat kAudioFormat{16000, 1, 16};
constexpr std::uint64_t kMinimumFreeBytes = 64 * 1024;
constexpr unsigned long kActiveDrawIntervalMs = 200;
constexpr std::uint8_t kPlaybackQueueCapacity = 2;
constexpr unsigned long kCaptureStartTimeoutMs = 1000;
constexpr unsigned long kCaptureDrainTimeoutMs = 1000;
constexpr unsigned long kSaveSettleMs = 750;
constexpr unsigned long kRecordingStopGuardMs = 500;

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

}  // namespace

void RecorderApp::begin()
{
    Serial.begin(115200);
    if (!board_.begin()) {
        setError("Cardputer ADV required.");
        return;
    }

    audio_.begin();
    power_.begin();
    updateBattery(true);
    auto& display = M5Cardputer.Display;
    display.setRotation(1);
    display.fillScreen(TFT_BLACK);
    display.setTextWrap(false);
    recorderCanvas.setColorDepth(8);
    recorderCanvas.createSprite(display.width(), display.height());
    recorderCanvas.setTextWrap(false);

    if (!storage_.begin()) {
        setError("Insert a writable microSD card.");
    } else {
        scanFiles();
        message_ = "R: record  Enter: play";
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
    if (state_ == State::kRecording) {
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
    if (state_ == State::kPlaying) {
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

bool RecorderApp::startRecording()
{
    if (!storage_.isMounted() && !storage_.remount()) {
        setError("E1: microSD mount failed.");
        return false;
    }
    const std::uint64_t capacity = storage_.capacityBytes();
    const std::uint64_t used = storage_.usedBytes();
    if (capacity <= used || capacity - used < kMinimumFreeBytes) {
        setError("E2: Not enough SD space.");
        return false;
    }

    char path[20];
    if (!chooseRecordingPath(path, sizeof(path))) {
        setError("E3: No free filename.");
        return false;
    }
    currentPath_ = path;
    const int recordingDescriptor =
        storage_.openWriteDescriptor(currentPath_.c_str());
    if (recordingDescriptor < 0) {
        currentPath_ = "";
        setError("E4: WAV file failed.");
        return false;
    }
    if (!writer_.beginStreaming(
            recordingDescriptor, kRecordingSpec)) {
        storage_.remove(currentPath_.c_str());
        currentPath_ = "";
        setError("E4: WAV file failed.");
        return false;
    }
    if (!audio_.startCapture(kAudioFormat)) {
        writer_.abort();
        storage_.remove(currentPath_.c_str());
        currentPath_ = "";
        setError("E5: Microphone start failed.");
        return false;
    }

    captureActiveHead_ = 0;
    captureActiveCount_ = 0;
    captureQueueActive_ = false;
    captureStarvationCount_ = 0;
    recordingLevel_.store(0, std::memory_order_relaxed);
    recordingBytes_.store(0, std::memory_order_relaxed);
    recordingNoiseFloor_ = 0.0F;
    if (!startCaptureWriter()) {
        audio_.stop();
        writer_.abort();
        storage_.remove(currentPath_.c_str());
        currentPath_ = "";
        setError("E6: Writer task failed.");
        return false;
    }
    if (!primeCapture()) {
        audio_.stop();
        stopCaptureWriter();
        writer_.abort();
        storage_.remove(currentPath_.c_str());
        currentPath_ = "";
        setError("E6: Mic queue failed.");
        return false;
    }

    const unsigned long captureWaitStartedMs = millis();
    while (audio_.captureQueueLevel() == 0 &&
           millis() - captureWaitStartedMs < kCaptureStartTimeoutMs) {
        delay(1);
    }
    if (audio_.captureQueueLevel() == 0) {
        audio_.stop();
        stopCaptureWriter();
        writer_.abort();
        storage_.remove(currentPath_.c_str());
        currentPath_ = "";
        setError("E7: Mic start timeout.");
        return false;
    }
    captureQueueActive_ = true;
    state_ = State::kRecording;
    operationStartedMs_ = millis();
    message_ = "Recording. Enter/Esc to stop.";
    forceRedraw_ = true;
    return true;
}

void RecorderApp::serviceRecording()
{
    if (captureWriterFailed_.load(std::memory_order_acquire)) {
        stopRecording(false);
        setError("E8: Recording SD write failed.");
        return;
    }

    const std::size_t queueLevel = audio_.captureQueueLevel();
    if (!captureQueueActive_) {
        if (queueLevel > 0) {
            captureQueueActive_ = true;
        }
        return;
    }

    if (queueLevel == 0 && captureActiveCount_ > 0) {
        ++captureStarvationCount_;
        Serial.printf(
            "[RECORDER] Capture queue starved count=%lu\n",
            static_cast<unsigned long>(captureStarvationCount_));
    }
    if (!serviceCompletedCapture(true)) {
        stopRecording(false);
        setError("E9: Capture buffer overflow.");
    }
}

bool RecorderApp::drainRecording()
{
    const unsigned long startedMs = millis();
    while (captureActiveCount_ > 0 &&
           millis() - startedMs < kCaptureDrainTimeoutMs) {
        if (!serviceCompletedCapture(false)) {
            return false;
        }
        if (captureActiveCount_ > 0) {
            delay(1);
        }
    }
    return captureActiveCount_ == 0;
}

void RecorderApp::stopRecording(bool keepFile)
{
    const bool drained =
        keepFile &&
        !captureWriterFailed_.load(std::memory_order_acquire) &&
        drainRecording();
    if (!drained) {
        audio_.stop();
    }
    stopCaptureWriter();
    const std::uint32_t recordedBytes = writer_.dataSize();
    const bool shouldSave =
        keepFile && drained &&
        !captureWriterFailed_.load(std::memory_order_acquire) &&
        recordedBytes > 0;
    bool fileClosed = true;

    // Commit the SD file before AudioService reconfigures I2S and the codec.
    // Leaving the file open across that teardown can produce a valid FAT
    // directory entry whose reported size remains zero on later recordings.
    if (shouldSave) {
        fileClosed = writer_.closeData();
    } else {
        writer_.abort();
    }

    audio_.stop();
    if (captureStarvationCount_ > 0) {
        Serial.printf(
            "[RECORDER] Recording ended with %lu capture starvation events\n",
            static_cast<unsigned long>(captureStarvationCount_));
    }
    Serial.printf(
        "[RECORDER] Writer queue peak=%u/%u\n",
        static_cast<unsigned>(captureWriterQueuePeak_),
        static_cast<unsigned>(kAudioBufferCount));
    captureActiveCount_ = 0;
    captureQueueActive_ = false;
    if (!shouldSave) {
        bool removed = false;
        for (std::uint8_t attempt = 0;
             attempt < 3 && !removed; ++attempt) {
            removed = storage_.remove(currentPath_.c_str());
            if (!removed) {
                delay(20);
            }
        }
        if (!removed) {
            Serial.println(
                "[RECORDER] Failed to remove discarded recording.");
        }
        state_ = keepFile ? State::kError : State::kBrowsing;
        message_ = keepFile ? "Save failed: capture drain."
                            : "Recording discarded.";
        currentPath_ = "";
        scanFiles();
        forceRedraw_ = true;
        return;
    }

    saveTotalBytes_ = recordedBytes;
    state_ = State::kSaving;
    forceRedraw_ = true;
    draw();
    if (!fileClosed) {
        finishSaving(false, "WAV finalize failed.");
        return;
    }
    if (!beginSaving()) {
        finishSaving(false, "Could not start save.");
    }
}

bool RecorderApp::beginSaving()
{
    saveCopiedBytes_ = saveTotalBytes_;
    saveAwaitingSettle_ = false;
    saveSettledAtMs_ = 0;
    if (saveTotalBytes_ == 0) {
        return false;
    }
    saveAwaitingSettle_ = true;
    saveSettledAtMs_ = millis() + kSaveSettleMs;
    message_ = "Syncing SD metadata...";
    return true;
}

void RecorderApp::serviceSaving()
{
    if (saveAwaitingSettle_) {
        if (millis() < saveSettledAtMs_) {
            return;
        }
        saveAwaitingSettle_ = false;
        finishSaving(true, "");
        return;
    }
}

void RecorderApp::finishSaving(bool success, const String& detail)
{
    String failureDetail = detail;
    if (success) {
        const std::uint32_t expectedSize =
            static_cast<std::uint32_t>(kPcmWavHeaderSize) +
            saveTotalBytes_;
        const std::uint32_t actualSize =
            storage_.fileSize(currentPath_.c_str());
        if (actualSize != expectedSize) {
            Serial.printf(
                "[RECORDER] WAV size mismatch path=%s "
                "expected=%lu actual=%lu\n",
                currentPath_.c_str(),
                static_cast<unsigned long>(expectedSize),
                static_cast<unsigned long>(actualSize));
            success = false;
            failureDetail =
                "WAV disk size " +
                String(static_cast<unsigned long>(actualSize)) +
                "/" +
                String(static_cast<unsigned long>(expectedSize));
        }
    }

    if (success) {
        message_ =
            "Saved " +
            String(static_cast<unsigned long>(
                saveTotalBytes_ / 1024)) +
            " KB";
        state_ = State::kBrowsing;
    } else {
        Serial.printf(
            "[RECORDER] WAV save failed path=%s bytes=%lu detail=%s\n",
            currentPath_.c_str(),
            static_cast<unsigned long>(saveTotalBytes_),
            failureDetail.c_str());
        message_ = failureDetail.length() > 0
                       ? failureDetail
                       : "WAV save failed; files retained.";
        state_ = State::kError;
    }

    currentPath_ = "";
    saveTotalBytes_ = 0;
    saveCopiedBytes_ = 0;
    saveSettledAtMs_ = 0;
    saveAwaitingSettle_ = false;
    if (storage_.isMounted()) {
        scanFiles();
    }
    forceRedraw_ = true;
}

bool RecorderApp::startPlayback()
{
    if (files_.empty()) {
        return false;
    }
    currentPath_ = "/" + files_[selected_];
    File file = storage_.open(currentPath_.c_str(), FILE_READ);
    if (!reader_.begin(file)) {
        WavError wavError = reader_.error();
        const std::uint32_t firstSize =
            file ? static_cast<std::uint32_t>(file.size()) : 0;
        file.close();
        reader_.end();

        // A fresh mount clears stale FAT/VFS state seen after repeated saves.
        if (storage_.remount()) {
            file = storage_.open(currentPath_.c_str(), FILE_READ);
        }
        if (!reader_.begin(file)) {
            wavError = reader_.error();
            const std::uint32_t retrySize =
                file ? static_cast<std::uint32_t>(file.size()) : 0;
            file.close();
            Serial.printf(
                "[RECORDER] WAV open failed path=%s error=%u "
                "size=%lu retrySize=%lu\n",
                currentPath_.c_str(),
                static_cast<unsigned int>(wavError),
                static_cast<unsigned long>(firstSize),
                static_cast<unsigned long>(retrySize));
            setError(
                "WAV error " +
                String(static_cast<unsigned int>(wavError)) +
                ", size " +
                String(static_cast<unsigned long>(retrySize)));
            return false;
        }
    }
    if (!audio_.startPlayback(playbackVolume_)) {
        reader_.end();
        setError("Speaker failed to start.");
        return false;
    }
    state_ = State::kPlaying;
    playbackBufferIndex_ = 0;
    const WavInfo& wav = reader_.info();
    const std::uint32_t bytesPerSecond =
        wav.spec.sampleRate * wav.spec.channels *
        (wav.spec.bitsPerSample / 8);
    playbackDurationMs_ =
        bytesPerSecond == 0
            ? 0
            : static_cast<std::uint32_t>(
                  static_cast<std::uint64_t>(wav.dataSize) * 1000 /
                  bytesPerSecond);
    audio_.setPlaybackVolume(playbackVolume_);
    operationStartedMs_ = millis();
    message_ = "UP/DOWN volume  Enter/Esc stop";
    forceRedraw_ = true;
    servicePlayback();
    return true;
}

void RecorderApp::servicePlayback()
{
    const AudioFormat format(
        reader_.info().spec.sampleRate,
        static_cast<std::uint8_t>(reader_.info().spec.channels),
        static_cast<std::uint8_t>(reader_.info().spec.bitsPerSample));

    while (audio_.playbackQueueLevel() < kPlaybackQueueCapacity &&
           !reader_.finished()) {
        std::int16_t* buffer = audioBuffers_[playbackBufferIndex_];
        const std::size_t count =
            reader_.readSamples(buffer, kAudioSamplesPerBuffer);
        if (count == 0) {
            stopPlayback();
            setError("Playback read failed.");
            return;
        }
        amplifyPlayback(buffer, count);
        if (!audio_.queuePlayback(buffer, count, format)) {
            stopPlayback();
            setError("Playback read failed.");
            return;
        }
        playbackBufferIndex_ =
            (playbackBufferIndex_ + 1) % kAudioBufferCount;
    }

    if (reader_.finished() && audio_.playbackQueueLevel() == 0) {
        stopPlayback();
        message_ = "Playback complete.";
        forceRedraw_ = true;
    }
}

void RecorderApp::stopPlayback()
{
    audio_.stop();
    reader_.end();
    state_ = State::kBrowsing;
    currentPath_ = "";
    playbackDurationMs_ = 0;
    message_ = "Playback stopped.";
    forceRedraw_ = true;
}

void RecorderApp::deleteSelected()
{
    const String path = "/" + files_[selected_];
    if (!storage_.remove(path.c_str())) {
        setError("Could not delete recording.");
        return;
    }
    message_ = "Deleted " + files_[selected_];
    scanFiles();
    forceRedraw_ = true;
}

void RecorderApp::scanFiles()
{
    files_.clear();
    File directory = storage_.open("/", FILE_READ);
    if (!directory || !directory.isDirectory()) {
        setError("Could not read SD directory.");
        return;
    }
    File entry = directory.openNextFile();
    while (entry) {
        String name = entry.name();
        String normalizedName = name;
        normalizedName.toLowerCase();
        if (!entry.isDirectory() &&
            normalizedName.endsWith(".wav") &&
            entry.size() >= kPcmWavHeaderSize) {
            if (name.startsWith("/")) {
                name.remove(0, 1);
            }
            files_.push_back(name);
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    if (files_.empty()) {
        selected_ = 0;
    } else if (selected_ >= static_cast<int>(files_.size())) {
        selected_ = files_.size() - 1;
    }
}

bool RecorderApp::chooseRecordingPath(char* path, std::size_t capacity)
{
    for (std::uint32_t index = 1; index <= 9999; ++index) {
        if (!makeRecordingPath(index, path, capacity)) {
            return false;
        }
        if (!storage_.exists(path)) {
            return true;
        }
    }
    return false;
}

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

void RecorderApp::adjustVolume(int offset)
{
    int next = static_cast<int>(playbackVolume_) + offset;
    if (next < 0) {
        next = 0;
    } else if (next > 255) {
        next = 255;
    }
    playbackVolume_ = static_cast<std::uint8_t>(next);
    audio_.setPlaybackVolume(playbackVolume_);
    forceRedraw_ = true;
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

unsigned long RecorderApp::playbackElapsedMs() const
{
    const unsigned long elapsed = millis() - operationStartedMs_;
    return playbackDurationMs_ == 0 || elapsed < playbackDurationMs_
               ? elapsed
               : playbackDurationMs_;
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
    display.print("VOICE");
    display.setTextFont(1);
    display.setTextColor(
        battery_.valid && battery_.levelPercent <= 15
            ? TFT_RED
            : TFT_LIGHTGREY,
        panel);
    display.setCursor(57, 8);
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
                          : (state_ == State::kError ? "ERROR"
                                                    : "LIBRARY")));
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
        if (files_.empty()) {
            display.drawRoundRect(14, 36, display.width() - 28, 62, 8,
                                  muted);
            display.fillCircle(display.width() / 2, 57, 10, TFT_RED);
            display.setTextFont(2);
            display.setTextDatum(middle_center);
            display.setTextColor(TFT_WHITE, background);
            display.drawString("No recordings",
                               display.width() / 2, 82);
            display.setTextDatum(top_left);
        } else {
            int first = selected_ - 1;
            if (first < 0) {
                first = 0;
            }
            if (first + 4 > static_cast<int>(files_.size())) {
                first = max(0, static_cast<int>(files_.size()) - 4);
            }
            for (int row = 0; row < 4 &&
                              first + row <
                                  static_cast<int>(files_.size());
                 ++row) {
                const int index = first + row;
                const int y = 29 + row * 20;
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
        display.drawString("DEL", display.width() - 8, 120);
        display.setTextDatum(top_left);
    }
    recorderCanvas.pushSprite(0, 0);
}

}  // namespace cardputer_recorder
