#include "recorder/recorder_app.h"

#include "recorder/app/app_shared.h"

namespace cardputer_recorder {
namespace {

constexpr int kPlaybackSeekSeconds = 10;

}  // namespace

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
    const std::uint32_t bytesPerSecond = playbackBytesPerSecond();
    playbackDurationMs_ =
        bytesPerSecond == 0
            ? 0
            : static_cast<std::uint32_t>(
                  static_cast<std::uint64_t>(wav.dataSize) * 1000 /
                  bytesPerSecond);
    audio_.setPlaybackVolume(playbackVolume_);
    playbackBaseElapsedMs_ = 0;
    playbackPaused_ = false;
    operationStartedMs_ = millis();
    message_ = "ENTER pause  ESC stop";
    forceRedraw_ = true;
    servicePlayback();
    return true;
}
void RecorderApp::servicePlayback()
{
    if (playbackPaused_) {
        return;
    }

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
    playbackBaseElapsedMs_ = 0;
    playbackPaused_ = false;
    message_ = "Playback stopped.";
    forceRedraw_ = true;
}
void RecorderApp::togglePlaybackPause()
{
    if (playbackPaused_) {
        if (!audio_.startPlayback(playbackVolume_)) {
            stopPlayback();
            setError("Speaker failed to start.");
            return;
        }
        playbackPaused_ = false;
        operationStartedMs_ = millis();
        message_ = "Playback resumed.";
        forceRedraw_ = true;
        servicePlayback();
        return;
    }

    playbackBaseElapsedMs_ = playbackElapsedMs();
    playbackPaused_ = true;
    message_ = "Playback paused.";
    forceRedraw_ = true;
}
bool RecorderApp::seekPlayback(int seconds)
{
    if (!reader_.isOpen()) {
        return false;
    }
    const std::uint32_t bytesPerSecond = playbackBytesPerSecond();
    if (bytesPerSecond == 0) {
        return false;
    }

    const std::int64_t currentMs = playbackElapsedMs();
    std::int64_t nextMs =
        currentMs + static_cast<std::int64_t>(seconds) * 1000;
    if (nextMs < 0) {
        nextMs = 0;
    } else if (playbackDurationMs_ > 0 &&
               nextMs > playbackDurationMs_) {
        nextMs = playbackDurationMs_;
    }

    const std::uint32_t targetByteOffset =
        static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(nextMs) * bytesPerSecond / 1000);
    audio_.stop();
    if (!reader_.seekDataByteOffset(targetByteOffset)) {
        stopPlayback();
        setError("Playback seek failed.");
        return false;
    }
    playbackBufferIndex_ = 0;
    playbackBaseElapsedMs_ = static_cast<std::uint32_t>(nextMs);
    operationStartedMs_ = millis();

    if (playbackBaseElapsedMs_ >= playbackDurationMs_) {
        playbackPaused_ = true;
        message_ = "Playback end.";
        forceRedraw_ = true;
        return true;
    }

    if (!playbackPaused_) {
        if (!audio_.startPlayback(playbackVolume_)) {
            stopPlayback();
            setError("Speaker failed to start.");
            return false;
        }
        servicePlayback();
    }
    message_ =
        seconds > 0
            ? "Forward " + String(kPlaybackSeekSeconds) + " sec."
            : "Back " + String(kPlaybackSeekSeconds) + " sec.";
    forceRedraw_ = true;
    return true;
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
    const unsigned long elapsed =
        playbackBaseElapsedMs_ +
        (playbackPaused_ ? 0 : millis() - operationStartedMs_);
    return playbackDurationMs_ == 0 || elapsed < playbackDurationMs_
               ? elapsed
               : playbackDurationMs_;
}
std::uint32_t RecorderApp::playbackBytesPerSecond() const
{
    const WavInfo& wav = reader_.info();
    return wav.spec.sampleRate * wav.spec.channels *
           (wav.spec.bitsPerSample / 8);
}

}  // namespace cardputer_recorder
