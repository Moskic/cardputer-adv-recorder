#pragma once

#include <cstdint>

namespace cardputer_recorder {

enum class ServiceState : std::uint8_t {
    kStopped,
    kStarting,
    kReady,
    kBusy,
    kError,
};

enum class ErrorCode : std::uint8_t {
    kNone,
    kInitializationFailed,
    kUnavailable,
    kBusy,
    kIo,
    kTimeout,
    kInvalidData,
    kUnsupported,
};

}  // namespace cardputer_recorder
