#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>

#include "recorder/hardware/audio_service.h"
#include "recorder/media/wav_format.h"

namespace cardputer_recorder {

constexpr WavSpec kRecordingSpec{16000, 1, 16};
constexpr AudioFormat kAudioFormat{16000, 1, 16};
constexpr std::uint64_t kMinimumFreeBytes = 64 * 1024;
constexpr unsigned long kActiveDrawIntervalMs = 200;
constexpr std::uint8_t kPlaybackQueueCapacity = 2;
constexpr unsigned long kCaptureStartTimeoutMs = 1000;
constexpr unsigned long kCaptureDrainTimeoutMs = 1000;
constexpr unsigned long kSaveSettleMs = 750;
constexpr unsigned long kRecordingStopGuardMs = 500;
constexpr std::uint8_t kSettingsCount = 5;
constexpr std::uint8_t kScreenSaverSettingsCount = 3;
constexpr unsigned long kIdleScreenSaverDelayMs = 30000;
constexpr unsigned long kActiveScreenSaverDelayMs = 15000;
constexpr std::uint8_t kDimBrightness = 12;

extern M5Canvas recorderCanvas;

String formatTime(unsigned long milliseconds);
String formatByteCount(std::uint64_t bytes);
const char* screenModeText(std::uint8_t mode);
const char* idleSleepText(std::uint8_t mode);
const char* playbackSpeedText(std::uint8_t index);

}  // namespace cardputer_recorder
