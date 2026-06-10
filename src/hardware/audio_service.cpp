#include "recorder/hardware/audio_service.h"

#include <Arduino.h>
#include <M5Cardputer.h>

namespace cardputer_recorder {
namespace {

constexpr std::uint8_t kEs8311Address = 0x18;
constexpr std::uint8_t kEs8311DacMuteRegister = 0x31;
constexpr std::uint8_t kEs8311DacVolumeRegister = 0x32;
constexpr std::uint8_t kEs8311HeadphoneRegister = 0x13;
constexpr std::uint8_t kDacMuteBits = 0x60;
constexpr std::uint8_t kEs8311HeadphoneEnabled = 0x10;
constexpr std::uint8_t kEs8311DacUnityGain = 0xBF;

}  // namespace

bool AudioService::begin()
{
    error_ = ErrorCode::kNone;
    state_ = ServiceState::kReady;
    return true;
}

void AudioService::update()
{
}

void AudioService::end()
{
    stop();
    state_ = ServiceState::kStopped;
}

bool AudioService::startCapture(const AudioFormat& format)
{
    stop();
    state_ = ServiceState::kStarting;
    error_ = ErrorCode::kNone;
    captureFormat_ = format;

    // ADV's ES8311 occasionally needs its output clock path initialized
    // before the first ADC start after boot.
    M5Cardputer.Speaker.setVolume(0);
    if (!M5Cardputer.Speaker.begin()) {
        state_ = ServiceState::kError;
        error_ = ErrorCode::kInitializationFailed;
        return false;
    }
    setCodecOutputMuted(true);
    delay(60);
    M5Cardputer.Speaker.stop();
    delay(10);
    M5Cardputer.Speaker.end();
    delay(40);

    auto micConfig = M5Cardputer.Mic.config();
    micConfig.magnification = 16;
    micConfig.noise_filter_level = 0;
    M5Cardputer.Mic.config(micConfig);

    if (!M5Cardputer.Mic.begin()) {
        state_ = ServiceState::kError;
        error_ = ErrorCode::kInitializationFailed;
        return false;
    }
    delay(80);
    setCodecOutputMuted(true);
    mode_ = AudioMode::kCapture;
    state_ = ServiceState::kReady;
    return true;
}

bool AudioService::restartCapture()
{
    const AudioFormat format = captureFormat_;
    stopCapture();
    return startCapture(format);
}

bool AudioService::record(std::int16_t* destination, std::size_t sampleCount)
{
    if (mode_ != AudioMode::kCapture || state_ == ServiceState::kError) {
        error_ = ErrorCode::kBusy;
        return false;
    }
    const bool queued = M5Cardputer.Mic.record(
        destination, sampleCount, captureFormat_.sampleRate);
    if (queued) {
        state_ = ServiceState::kBusy;
    }
    return queued;
}

bool AudioService::isRecording() const
{
    return mode_ == AudioMode::kCapture && M5Cardputer.Mic.isRecording();
}

std::size_t AudioService::captureQueueLevel() const
{
    return mode_ == AudioMode::kCapture
               ? M5Cardputer.Mic.isRecording()
               : 0;
}

bool AudioService::startPlayback(std::uint8_t volume)
{
    stop();
    state_ = ServiceState::kStarting;
    error_ = ErrorCode::kNone;
    auto speakerConfig = M5Cardputer.Speaker.config();
    speakerConfig.magnification = 32;
    M5Cardputer.Speaker.config(speakerConfig);
    if (!M5Cardputer.Speaker.begin()) {
        setCodecOutputMuted(true);
        state_ = ServiceState::kError;
        error_ = ErrorCode::kInitializationFailed;
        return false;
    }
    setCodecOutputMuted(false);
    M5Cardputer.Speaker.setVolume(volume);
    mode_ = AudioMode::kPlayback;
    state_ = ServiceState::kReady;
    return true;
}

bool AudioService::playTone(float frequency, std::uint32_t durationMs)
{
    if (mode_ != AudioMode::kPlayback) {
        error_ = ErrorCode::kBusy;
        return false;
    }
    const bool queued = M5Cardputer.Speaker.tone(frequency, durationMs);
    if (queued) {
        state_ = ServiceState::kBusy;
    }
    return queued;
}

bool AudioService::queuePlayback(const std::int16_t* samples,
                                 std::size_t sampleCount,
                                 const AudioFormat& format,
                                 bool stopCurrent)
{
    if (mode_ != AudioMode::kPlayback || format.channels != 1 ||
        format.bitsPerSample != 16) {
        error_ = ErrorCode::kUnsupported;
        return false;
    }
    const bool queued = M5Cardputer.Speaker.playRaw(
        samples, sampleCount, format.sampleRate, false, 1, 0, stopCurrent);
    if (queued) {
        state_ = ServiceState::kBusy;
    }
    return queued;
}

bool AudioService::isPlaying() const
{
    return mode_ == AudioMode::kPlayback &&
           M5Cardputer.Speaker.isPlaying();
}

std::size_t AudioService::playbackQueueLevel(std::uint8_t channel) const
{
    return mode_ == AudioMode::kPlayback
               ? M5Cardputer.Speaker.isPlaying(channel)
               : 0;
}

void AudioService::setPlaybackVolume(std::uint8_t volume)
{
    if (mode_ == AudioMode::kPlayback) {
        M5Cardputer.Speaker.setVolume(volume);
    }
}

std::uint8_t AudioService::playbackVolume() const
{
    return M5Cardputer.Speaker.getVolume();
}

void AudioService::stop()
{
    if (mode_ == AudioMode::kCapture) {
        stopCapture();
    } else if (mode_ == AudioMode::kPlayback) {
        stopPlayback();
    } else {
        setCodecOutputMuted(true);
    }
    mode_ = AudioMode::kIdle;
    if (state_ != ServiceState::kStopped) {
        state_ = ServiceState::kReady;
    }
}

AudioMode AudioService::mode() const
{
    return mode_;
}

ServiceState AudioService::state() const
{
    return state_;
}

ErrorCode AudioService::lastError() const
{
    return error_;
}

void AudioService::stopCapture()
{
    const bool wasRunning = M5Cardputer.Mic.isRunning();
    if (wasRunning) {
        setCodecOutputMuted(true);
    }
    M5Cardputer.Mic.end();
    delay(30);

    if (wasRunning) {
        M5Cardputer.Speaker.setVolume(0);
        M5Cardputer.Speaker.begin();
        setCodecOutputMuted(true);
        delay(40);
        M5Cardputer.Speaker.stop();
        delay(10);
        M5Cardputer.Speaker.end();
        delay(20);
    }
}

void AudioService::stopPlayback()
{
    M5Cardputer.Speaker.stop();
    delay(10);
    setCodecOutputMuted(true);
    M5Cardputer.Speaker.end();
    delay(10);
}

void AudioService::setCodecOutputMuted(bool muted)
{
    if (muted) {
        M5Cardputer.In_I2C.writeRegister8(
            kEs8311Address, kEs8311DacVolumeRegister, 0x00, 100000);
        M5Cardputer.In_I2C.writeRegister8(
            kEs8311Address, kEs8311HeadphoneRegister, 0x00, 100000);
    } else {
        // Restore the Cardputer ADV values used by M5Unified's ES8311
        // speaker callback. Clearing only the mute bits leaves the DAC and
        // headphone driver at zero after microphone mode.
        M5Cardputer.In_I2C.writeRegister8(
            kEs8311Address, kEs8311HeadphoneRegister,
            kEs8311HeadphoneEnabled, 100000);
        M5Cardputer.In_I2C.writeRegister8(
            kEs8311Address, kEs8311DacVolumeRegister,
            kEs8311DacUnityGain, 100000);
    }
    M5Cardputer.In_I2C.writeRegister8(
        kEs8311Address, kEs8311DacMuteRegister,
        muted ? kDacMuteBits : 0x00, 100000);
}

}  // namespace cardputer_recorder
