#include "recorder/media/wav_format.h"

#include <cstring>

namespace cardputer_recorder {
namespace {

std::uint16_t read16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1]) << 8;
}

std::uint32_t read32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           static_cast<std::uint32_t>(data[1]) << 8 |
           static_cast<std::uint32_t>(data[2]) << 16 |
           static_cast<std::uint32_t>(data[3]) << 24;
}

void write16(std::uint8_t* data, std::uint16_t value)
{
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
}

void write32(std::uint8_t* data, std::uint32_t value)
{
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
    data[2] = static_cast<std::uint8_t>(value >> 16);
    data[3] = static_cast<std::uint8_t>(value >> 24);
}

bool hasId(const std::uint8_t* data, const char* id)
{
    return std::memcmp(data, id, 4) == 0;
}

}  // namespace

void encodePcmWavHeader(std::uint8_t* destination, const WavSpec& spec,
                        std::uint32_t dataSize)
{
    std::memset(destination, 0, kPcmWavHeaderSize);
    std::memcpy(destination, "RIFF", 4);
    write32(destination + 4, 36 + dataSize);
    std::memcpy(destination + 8, "WAVE", 4);
    std::memcpy(destination + 12, "fmt ", 4);
    write32(destination + 16, 16);
    write16(destination + 20, 1);
    write16(destination + 22, spec.channels);
    write32(destination + 24, spec.sampleRate);
    const std::uint16_t blockAlign =
        spec.channels * (spec.bitsPerSample / 8);
    write32(destination + 28, spec.sampleRate * blockAlign);
    write16(destination + 32, blockAlign);
    write16(destination + 34, spec.bitsPerSample);
    std::memcpy(destination + 36, "data", 4);
    write32(destination + 40, dataSize);
}

void encodeStreamingPcmWavHeader(std::uint8_t* destination,
                                 const WavSpec& spec)
{
    encodePcmWavHeader(destination, spec, 0);
    write32(destination + 4, UINT32_MAX);
    write32(destination + 40, UINT32_MAX);
}

WavError parsePcmWavHeader(const std::uint8_t* header,
                           std::size_t headerSize,
                           std::uint32_t fileSize,
                           WavInfo& output)
{
    output = {};
    if (header == nullptr || headerSize < kPcmWavHeaderSize) {
        return WavError::kTruncated;
    }
    if (!hasId(header, "RIFF") || !hasId(header + 8, "WAVE")) {
        return WavError::kInvalidRiff;
    }
    if (!hasId(header + 12, "fmt ") || read32(header + 16) < 16) {
        return WavError::kMissingFormat;
    }
    if (read16(header + 20) != 1) {
        return WavError::kUnsupportedFormat;
    }

    output.spec.channels = read16(header + 22);
    output.spec.sampleRate = read32(header + 24);
    output.spec.bitsPerSample = read16(header + 34);
    if (output.spec.channels == 0 || output.spec.channels > 2 ||
        (output.spec.bitsPerSample != 8 &&
         output.spec.bitsPerSample != 16)) {
        return WavError::kUnsupportedFormat;
    }
    if (!hasId(header + 36, "data")) {
        return WavError::kMissingData;
    }

    output.dataOffset = kPcmWavHeaderSize;
    output.dataSize = read32(header + 40);
    if (output.dataSize == UINT32_MAX) {
        output.dataSize =
            fileSize > output.dataOffset
                ? fileSize - output.dataOffset
                : 0;
    }
    if (static_cast<std::uint64_t>(output.dataOffset) +
            output.dataSize >
        fileSize) {
        return WavError::kTruncated;
    }
    return WavError::kNone;
}

WavError parseWav(const std::uint8_t* data, std::size_t size,
                  WavInfo& output)
{
    output = {};
    if (size < 12) {
        return WavError::kTruncated;
    }
    if (!hasId(data, "RIFF") || !hasId(data + 8, "WAVE")) {
        return WavError::kInvalidRiff;
    }

    bool formatFound = false;
    bool dataFound = false;
    std::size_t offset = 12;
    while (offset + 8 <= size) {
        const std::uint8_t* chunk = data + offset;
        const std::uint32_t chunkSize = read32(chunk + 4);
        const std::size_t payloadOffset = offset + 8;
        if (payloadOffset + chunkSize > size) {
            return WavError::kTruncated;
        }

        if (hasId(chunk, "fmt ")) {
            if (chunkSize < 16) {
                return WavError::kTruncated;
            }
            const std::uint16_t format = read16(data + payloadOffset);
            output.spec.channels = read16(data + payloadOffset + 2);
            output.spec.sampleRate = read32(data + payloadOffset + 4);
            output.spec.bitsPerSample =
                read16(data + payloadOffset + 14);
            if (format != 1 || output.spec.channels == 0 ||
                output.spec.channels > 2 ||
                (output.spec.bitsPerSample != 8 &&
                 output.spec.bitsPerSample != 16)) {
                return WavError::kUnsupportedFormat;
            }
            formatFound = true;
        } else if (hasId(chunk, "data")) {
            output.dataOffset = payloadOffset;
            output.dataSize = chunkSize;
            dataFound = true;
        }

        offset = payloadOffset + chunkSize + (chunkSize & 1U);
    }

    if (!formatFound) {
        return WavError::kMissingFormat;
    }
    if (!dataFound) {
        return WavError::kMissingData;
    }
    return WavError::kNone;
}

}  // namespace cardputer_recorder
