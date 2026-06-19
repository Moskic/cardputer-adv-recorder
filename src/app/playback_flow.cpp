#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {

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
unsigned long RecorderApp::playbackElapsedMs() const
{
    const unsigned long elapsed = millis() - operationStartedMs_;
    return playbackDurationMs_ == 0 || elapsed < playbackDurationMs_
               ? elapsed
               : playbackDurationMs_;
}

}  // namespace cardputer_recorder
