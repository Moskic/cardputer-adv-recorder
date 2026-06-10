#pragma once

#include <cstddef>
#include <cstdint>

#include "recorder/hardware/service.h"

namespace cardputer_recorder {

struct AudioFormat {
    constexpr AudioFormat(std::uint32_t rate = 16000,
                          std::uint8_t channelCount = 1,
                          std::uint8_t bitDepth = 16)
        : sampleRate(rate),
          channels(channelCount),
          bitsPerSample(bitDepth)
    {
    }

    std::uint32_t sampleRate;
    std::uint8_t channels;
    std::uint8_t bitsPerSample;
};

enum class AudioMode : std::uint8_t {
    kIdle,
    kCapture,
    kPlayback,
};

class AudioService {
public:
    bool begin();
    void update();
    void end();

    bool startCapture(const AudioFormat& format = {});
    bool restartCapture();
    bool record(std::int16_t* destination, std::size_t sampleCount);
    bool isRecording() const;
    std::size_t captureQueueLevel() const;

    bool startPlayback(std::uint8_t volume = 160);
    bool playTone(float frequency, std::uint32_t durationMs);
    bool queuePlayback(const std::int16_t* samples, std::size_t sampleCount,
                       const AudioFormat& format, bool stopCurrent = false);
    bool isPlaying() const;
    std::size_t playbackQueueLevel(std::uint8_t channel = 0) const;
    void setPlaybackVolume(std::uint8_t volume);
    std::uint8_t playbackVolume() const;
    void stop();

    AudioMode mode() const;
    ServiceState state() const;
    ErrorCode lastError() const;

private:
    void stopCapture();
    void stopPlayback();
    void setCodecOutputMuted(bool muted);

    AudioFormat captureFormat_;
    AudioMode mode_ = AudioMode::kIdle;
    ServiceState state_ = ServiceState::kStopped;
    ErrorCode error_ = ErrorCode::kNone;
};

}  // namespace cardputer_recorder
