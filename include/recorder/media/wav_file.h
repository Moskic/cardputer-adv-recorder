#pragma once

#include <FS.h>

#include "recorder/media/wav_format.h"

namespace cardputer_recorder {

enum class WavFinalizeError : std::uint8_t {
    kNone,
    kNotOpen,
    kEmpty,
    kSeek,
    kHeaderWrite,
    kSync,
    kClose,
};

class WavWriter {
public:
    bool begin(File&& file, const WavSpec& spec);
    bool beginStreaming(File&& file, const WavSpec& spec);
    bool beginStreaming(int descriptor, const WavSpec& spec);
    bool writeSamples(const std::int16_t* samples, std::size_t sampleCount);
    bool finalize();
    bool closeData();
    bool save(File file, const std::int16_t* samples,
              std::size_t sampleCount, const WavSpec& spec);
    void abort();
    bool isOpen() const;
    std::uint32_t dataSize() const;
    std::size_t lastWriteRequested() const;
    std::size_t lastWriteCompleted() const;
    int lastWriteError() const;
    WavFinalizeError finalizeError() const;

private:
    File file_;
    int descriptor_ = -1;
    WavSpec spec_;
    std::uint32_t dataSize_ = 0;
    std::size_t lastWriteRequested_ = 0;
    std::size_t lastWriteCompleted_ = 0;
    int lastWriteError_ = 0;
    WavFinalizeError finalizeError_ = WavFinalizeError::kNone;
};

class WavReader {
public:
    bool begin(File file);
    std::size_t readSamples(std::int16_t* destination,
                            std::size_t sampleCapacity);
    bool seekDataByteOffset(std::uint32_t byteOffset);
    void end();
    bool isOpen() const;
    bool finished() const;
    std::uint32_t bytesRead() const;
    const WavInfo& info() const;
    WavError error() const;

private:
    File file_;
    WavInfo info_;
    WavError error_ = WavError::kNone;
    std::uint32_t bytesRead_ = 0;
};

}  // namespace cardputer_recorder
