#include "recorder/hardware/storage_service.h"

#include <SD.h>
#include <SPI.h>
#include <fcntl.h>
#include <unistd.h>

namespace cardputer_recorder {
namespace {

constexpr std::uint8_t kSdClockPin = 40;
constexpr std::uint8_t kSdMisoPin = 39;
constexpr std::uint8_t kSdMosiPin = 14;
constexpr std::uint8_t kSdChipSelectPin = 12;
// Cardputer ADV wiring and microSD cards vary in signal quality. A lower
// clock is substantially more reliable for sustained recorder traffic.
constexpr std::uint32_t kSdFrequencyHz = 10000000;

}  // namespace

bool StorageService::begin()
{
    if (state_ == ServiceState::kReady) {
        return true;
    }

    state_ = ServiceState::kStarting;
    error_ = ErrorCode::kNone;
    SPI.begin(kSdClockPin, kSdMisoPin, kSdMosiPin, kSdChipSelectPin);
    if (!SD.begin(kSdChipSelectPin, SPI, kSdFrequencyHz) ||
        SD.cardType() == CARD_NONE) {
        SD.end();
        SPI.end();
        state_ = ServiceState::kError;
        error_ = ErrorCode::kUnavailable;
        return false;
    }

    state_ = ServiceState::kReady;
    return true;
}

bool StorageService::remount()
{
    end();
    delay(50);
    return begin();
}

void StorageService::update()
{
}

void StorageService::end()
{
    if (state_ != ServiceState::kStopped) {
        SD.end();
        SPI.end();
    }
    state_ = ServiceState::kStopped;
}

ServiceState StorageService::state() const
{
    return state_;
}

ErrorCode StorageService::lastError() const
{
    return error_;
}

bool StorageService::isMounted() const
{
    return state_ == ServiceState::kReady;
}

std::uint64_t StorageService::capacityBytes() const
{
    return isMounted() ? SD.cardSize() : 0;
}

std::uint64_t StorageService::usedBytes() const
{
    return isMounted() ? SD.usedBytes() : 0;
}

std::uint32_t StorageService::fileSize(const char* path)
{
    File file = open(path, FILE_READ);
    if (!file) {
        return 0;
    }
    const std::uint32_t size =
        static_cast<std::uint32_t>(file.size());
    file.close();
    return size;
}

File StorageService::open(const char* path, const char* mode)
{
    if (!isMounted()) {
        error_ = ErrorCode::kUnavailable;
        return File();
    }
    File file = SD.open(path, mode);
    if (!file) {
        error_ = ErrorCode::kIo;
    } else {
        error_ = ErrorCode::kNone;
    }
    return file;
}

int StorageService::openWriteDescriptor(const char* path)
{
    if (!isMounted() || path == nullptr || path[0] != '/') {
        error_ = ErrorCode::kUnavailable;
        return -1;
    }

    String mountedPath = "/sd";
    mountedPath += path;
    const int descriptor = ::open(
        mountedPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0) {
        error_ = ErrorCode::kIo;
    } else {
        error_ = ErrorCode::kNone;
    }
    return descriptor;
}

bool StorageService::exists(const char* path) const
{
    return isMounted() && SD.exists(path);
}

bool StorageService::remove(const char* path)
{
    if (!isMounted() || !SD.remove(path)) {
        error_ = ErrorCode::kIo;
        return false;
    }
    error_ = ErrorCode::kNone;
    return true;
}

bool StorageService::rename(const char* from, const char* to)
{
    if (!isMounted() || !SD.rename(from, to)) {
        error_ = ErrorCode::kIo;
        return false;
    }
    error_ = ErrorCode::kNone;
    return true;
}

}  // namespace cardputer_recorder
