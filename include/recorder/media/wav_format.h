#pragma once

#include <cstddef>
#include <cstdint>

namespace cardputer_recorder {

struct WavSpec {
    constexpr WavSpec(std::uint32_t rate = 16000,
                      std::uint16_t channelCount = 1,
                      std::uint16_t bitDepth = 16)
        : sampleRate(rate),
          channels(channelCount),
          bitsPerSample(bitDepth)
    {
    }

    std::uint32_t sampleRate;
    std::uint16_t channels;
    std::uint16_t bitsPerSample;
};

struct WavInfo {
    WavSpec spec;
    std::uint32_t dataOffset = 0;
    std::uint32_t dataSize = 0;
};

enum class WavError : std::uint8_t {
    kNone,
    kTruncated,
    kInvalidRiff,
    kMissingFormat,
    kMissingData,
    kUnsupportedFormat,
};

constexpr std::size_t kPcmWavHeaderSize = 44;

void encodePcmWavHeader(std::uint8_t* destination, const WavSpec& spec,
                        std::uint32_t dataSize);
void encodeStreamingPcmWavHeader(std::uint8_t* destination,
                                 const WavSpec& spec);
WavError parsePcmWavHeader(const std::uint8_t* header,
                           std::size_t headerSize,
                           std::uint32_t fileSize,
                           WavInfo& output);
WavError parseWav(const std::uint8_t* data, std::size_t size,
                  WavInfo& output);

}  // namespace cardputer_recorder
