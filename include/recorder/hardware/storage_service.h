#pragma once

#include <Arduino.h>
#include <FS.h>

#include "recorder/hardware/service.h"

namespace cardputer_recorder {

class StorageService {
public:
    bool begin();
    bool remount();
    void update();
    void end();

    ServiceState state() const;
    ErrorCode lastError() const;
    bool isMounted() const;
    std::uint64_t capacityBytes() const;
    std::uint64_t usedBytes() const;
    std::uint32_t fileSize(const char* path);

    File open(const char* path, const char* mode = FILE_READ);
    int openWriteDescriptor(const char* path);
    bool exists(const char* path) const;
    bool remove(const char* path);
private:
    ServiceState state_ = ServiceState::kStopped;
    ErrorCode error_ = ErrorCode::kNone;
};

}  // namespace cardputer_recorder
