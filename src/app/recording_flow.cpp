#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

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
    lowBatteryAutoSave_ = false;
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
    if (shouldAutoSaveForLowBattery()) {
        lowBatteryAutoSave_ = true;
        message_ = "Low battery: saving...";
        stopRecording(true);
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
        lowBatteryAutoSave_ = false;
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
        message_ = lowBatteryAutoSave_
                       ? "Low battery: saved " +
                             String(static_cast<unsigned long>(
                                 saveTotalBytes_ / 1024)) +
                             " KB"
                       : "Saved " +
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
    lowBatteryAutoSave_ = false;
    saveSettledAtMs_ = 0;
    saveAwaitingSettle_ = false;
    if (storage_.isMounted()) {
        scanFiles();
    }
    forceRedraw_ = true;
}

}  // namespace cardputer_recorder
