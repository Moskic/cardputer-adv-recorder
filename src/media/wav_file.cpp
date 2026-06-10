#include "recorder/media/wav_file.h"

#include <cerrno>
#include <unistd.h>

namespace cardputer_recorder {
namespace {

constexpr std::size_t kSdWriteChunkBytes = 512;
constexpr std::uint8_t kWriteRetryCount = 3;
constexpr std::uint8_t kDescriptorWriteRetryCount = 20;

bool writeDescriptorFully(int descriptor, const std::uint8_t* data,
                          std::size_t size, std::size_t& completed,
                          int& writeError)
{
    completed = 0;
    writeError = 0;
    std::uint8_t failedAttempts = 0;
    while (completed < size) {
        const ssize_t written =
            ::write(descriptor, data + completed, size - completed);
        if (written > 0) {
            completed += static_cast<std::size_t>(written);
            failedAttempts = 0;
            continue;
        }

        writeError = written < 0 ? errno : EIO;
        if (writeError == EINTR) {
            continue;
        }
        if (++failedAttempts >= kDescriptorWriteRetryCount) {
            return false;
        }
        delay(2);
    }
    return true;
}

}  // namespace

bool WavWriter::begin(File&& file, const WavSpec& spec)
{
    return beginStreaming(std::move(file), spec);
}

bool WavWriter::beginStreaming(File&& file, const WavSpec& spec)
{
    abort();
    if (!file || spec.channels != 1 || spec.bitsPerSample != 16) {
        return false;
    }
    file_ = std::move(file);
    spec_ = spec;
    dataSize_ = 0;
    lastWriteRequested_ = 0;
    lastWriteCompleted_ = 0;
    lastWriteError_ = 0;
    finalizeError_ = WavFinalizeError::kNone;
    std::uint8_t header[kPcmWavHeaderSize];
    encodeStreamingPcmWavHeader(header, spec_);
    if (file_.write(header, sizeof(header)) != sizeof(header)) {
        abort();
        return false;
    }
    return true;
}

bool WavWriter::beginStreaming(int descriptor, const WavSpec& spec)
{
    abort();
    if (descriptor < 0 || spec.channels != 1 ||
        spec.bitsPerSample != 16) {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
        return false;
    }

    descriptor_ = descriptor;
    spec_ = spec;
    dataSize_ = 0;
    lastWriteRequested_ = 0;
    lastWriteCompleted_ = 0;
    lastWriteError_ = 0;
    finalizeError_ = WavFinalizeError::kNone;

    std::uint8_t header[kPcmWavHeaderSize];
    encodeStreamingPcmWavHeader(header, spec_);
    std::size_t completed = 0;
    int writeError = 0;
    if (!writeDescriptorFully(
            descriptor_, header, sizeof(header), completed,
            writeError)) {
        lastWriteError_ = writeError;
        abort();
        return false;
    }
    return true;
}

bool WavWriter::writeSamples(const std::int16_t* samples,
                             std::size_t sampleCount)
{
    if ((!file_ && descriptor_ < 0) || samples == nullptr) {
        return false;
    }
    const std::size_t bytes = sampleCount * sizeof(std::int16_t);
    const auto* source =
        reinterpret_cast<const std::uint8_t*>(samples);
    lastWriteRequested_ = bytes;
    lastWriteCompleted_ = 0;
    lastWriteError_ = 0;

    while (lastWriteCompleted_ < bytes) {
        const std::size_t remaining = bytes - lastWriteCompleted_;
        const std::size_t chunk =
            remaining < kSdWriteChunkBytes
                ? remaining
                : kSdWriteChunkBytes;

        std::size_t chunkWritten = 0;
        if (descriptor_ >= 0) {
            if (!writeDescriptorFully(
                    descriptor_,
                    source + lastWriteCompleted_,
                    chunk, chunkWritten, lastWriteError_)) {
                lastWriteCompleted_ += chunkWritten;
                dataSize_ += chunkWritten;
                return false;
            }
        } else {
            for (std::uint8_t attempt = 0;
                 attempt < kWriteRetryCount && chunkWritten < chunk;
                 ++attempt) {
                const std::size_t written = file_.write(
                    source + lastWriteCompleted_ + chunkWritten,
                    chunk - chunkWritten);
                chunkWritten += written;
                if (chunkWritten < chunk) {
                    delay(1);
                }
            }
        }

        lastWriteCompleted_ += chunkWritten;
        dataSize_ += chunkWritten;
        if (chunkWritten != chunk) {
            return false;
        }
    }
    return true;
}

bool WavWriter::closeData()
{
    finalizeError_ = WavFinalizeError::kNone;
    if (descriptor_ >= 0) {
        const int descriptor = descriptor_;
        descriptor_ = -1;

        bool finalized = true;
        if (dataSize_ == 0) {
            finalizeError_ = WavFinalizeError::kEmpty;
            finalized = false;
        } else if (::lseek(descriptor, 0, SEEK_SET) < 0) {
            finalizeError_ = WavFinalizeError::kSeek;
            finalized = false;
        } else {
            std::uint8_t header[kPcmWavHeaderSize];
            encodePcmWavHeader(header, spec_, dataSize_);
            std::size_t completed = 0;
            int writeError = 0;
            if (!writeDescriptorFully(
                    descriptor, header, sizeof(header), completed,
                    writeError)) {
                lastWriteError_ = writeError;
                finalizeError_ = WavFinalizeError::kHeaderWrite;
                finalized = false;
            }
        }

        if (finalized && ::fsync(descriptor) != 0) {
            finalizeError_ = WavFinalizeError::kSync;
            finalized = false;
        }
        if (::close(descriptor) != 0) {
            if (finalized) {
                finalizeError_ = WavFinalizeError::kClose;
            }
            finalized = false;
        }
        return finalized;
    }
    return finalize();
}

bool WavWriter::finalize()
{
    finalizeError_ = WavFinalizeError::kNone;
    if (descriptor_ >= 0) {
        return closeData();
    }
    if (!file_) {
        finalizeError_ = WavFinalizeError::kNotOpen;
        return false;
    }
    if (dataSize_ == 0) {
        finalizeError_ = WavFinalizeError::kEmpty;
        file_.close();
        file_ = File();
        return false;
    }

    // Keep the original FILE_WRITE handle for the entire recording. The
    // ESP32 VFS maps it to stdio "w", which supports seeking and avoids an
    // unreliable close/reopen cycle on SD cards.
    file_.flush();
    if (!file_.seek(0)) {
        finalizeError_ = WavFinalizeError::kSeek;
        file_.close();
        file_ = File();
        return false;
    }

    std::uint8_t header[kPcmWavHeaderSize];
    encodePcmWavHeader(header, spec_, dataSize_);
    std::size_t written = 0;
    while (written < sizeof(header)) {
        const std::size_t count =
            file_.write(header + written, sizeof(header) - written);
        if (count == 0) {
            finalizeError_ = WavFinalizeError::kHeaderWrite;
            file_.close();
            file_ = File();
            return false;
        }
        written += count;
    }

    file_.flush();
    delay(10);
    file_.close();
    file_ = File();
    return true;
}

bool WavWriter::save(File file, const std::int16_t* samples,
                     std::size_t sampleCount, const WavSpec& spec)
{
    abort();
    if (!file || samples == nullptr || sampleCount == 0 ||
        spec.channels != 1 || spec.bitsPerSample != 16) {
        return false;
    }

    std::uint8_t header[kPcmWavHeaderSize];
    const std::size_t audioBytes =
        sampleCount * sizeof(std::int16_t);
    encodePcmWavHeader(
        header, spec, static_cast<std::uint32_t>(audioBytes));
    lastWriteRequested_ = sizeof(header);
    lastWriteCompleted_ = file.write(header, sizeof(header));
    if (lastWriteCompleted_ != lastWriteRequested_) {
        file.close();
        return false;
    }

    const auto* source =
        reinterpret_cast<const std::uint8_t*>(samples);
    lastWriteRequested_ = audioBytes;
    lastWriteCompleted_ = 0;
    while (lastWriteCompleted_ < audioBytes) {
        const std::size_t remaining =
            audioBytes - lastWriteCompleted_;
        const std::size_t requested =
            remaining < kSdWriteChunkBytes
                ? remaining
                : kSdWriteChunkBytes;
        const std::size_t written =
            file.write(source + lastWriteCompleted_, requested);
        if (written == 0) {
            file.close();
            return false;
        }
        lastWriteCompleted_ += written;
    }
    file.close();
    dataSize_ = audioBytes;
    return true;
}

void WavWriter::abort()
{
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
    if (file_) {
        file_.close();
        file_ = File();
    }
}

bool WavWriter::isOpen() const
{
    return descriptor_ >= 0 || static_cast<bool>(file_);
}

std::uint32_t WavWriter::dataSize() const
{
    return dataSize_;
}

std::size_t WavWriter::lastWriteRequested() const
{
    return lastWriteRequested_;
}

std::size_t WavWriter::lastWriteCompleted() const
{
    return lastWriteCompleted_;
}

int WavWriter::lastWriteError() const
{
    return lastWriteError_;
}

WavFinalizeError WavWriter::finalizeError() const
{
    return finalizeError_;
}

bool WavReader::begin(File file)
{
    end();
    info_ = {};
    error_ = WavError::kNone;
    if (!file || file.size() < kPcmWavHeaderSize) {
        error_ = WavError::kTruncated;
        return false;
    }

    // Files produced by this project use the canonical 44-byte PCM header.
    // Parse that path sequentially so playback does not depend on SD seek,
    // which is unreliable on some Cardputer ADV card/driver combinations.
    const std::uint32_t fileSize =
        static_cast<std::uint32_t>(file.size());
    std::uint8_t fixedHeader[kPcmWavHeaderSize];
    const std::size_t fixedHeaderRead =
        file.read(fixedHeader, sizeof(fixedHeader));
    if (fixedHeaderRead == sizeof(fixedHeader)) {
        WavInfo fixedInfo;
        const WavError fixedError =
            parsePcmWavHeader(
                fixedHeader, sizeof(fixedHeader), fileSize, fixedInfo);
        if (fixedError == WavError::kNone) {
            info_ = fixedInfo;
            file_ = file;
            bytesRead_ = 0;
            return true;
        }
    }

    if (!file.seek(0)) {
        error_ = WavError::kTruncated;
        return false;
    }
    std::uint8_t riff[12];
    if (file.read(riff, sizeof(riff)) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 ||
        memcmp(riff + 8, "WAVE", 4) != 0) {
        error_ = WavError::kInvalidRiff;
        return false;
    }

    auto read16 = [](const std::uint8_t* data) {
        return static_cast<std::uint16_t>(data[0]) |
               static_cast<std::uint16_t>(data[1]) << 8;
    };
    auto read32 = [](const std::uint8_t* data) {
        return static_cast<std::uint32_t>(data[0]) |
               static_cast<std::uint32_t>(data[1]) << 8 |
               static_cast<std::uint32_t>(data[2]) << 16 |
               static_cast<std::uint32_t>(data[3]) << 24;
    };

    bool formatFound = false;
    bool dataFound = false;
    std::uint32_t offset = 12;
    while (offset + 8 <= file.size()) {
        if (!file.seek(offset)) {
            break;
        }
        std::uint8_t chunk[8];
        if (file.read(chunk, sizeof(chunk)) != sizeof(chunk)) {
            error_ = WavError::kTruncated;
            return false;
        }
        const std::uint32_t chunkSize = read32(chunk + 4);
        const std::uint32_t payloadOffset = offset + 8;
        if (payloadOffset + chunkSize > file.size()) {
            error_ = WavError::kTruncated;
            return false;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                error_ = WavError::kTruncated;
                return false;
            }
            std::uint8_t format[16];
            if (file.read(format, sizeof(format)) != sizeof(format)) {
                error_ = WavError::kTruncated;
                return false;
            }
            info_.spec.channels = read16(format + 2);
            info_.spec.sampleRate = read32(format + 4);
            info_.spec.bitsPerSample = read16(format + 14);
            if (read16(format) != 1 || info_.spec.channels == 0 ||
                info_.spec.channels > 2 ||
                (info_.spec.bitsPerSample != 8 &&
                 info_.spec.bitsPerSample != 16)) {
                error_ = WavError::kUnsupportedFormat;
                return false;
            }
            formatFound = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            info_.dataOffset = payloadOffset;
            info_.dataSize = chunkSize;
            dataFound = true;
        }
        offset = payloadOffset + chunkSize + (chunkSize & 1U);
    }

    if (!formatFound || !dataFound) {
        error_ =
            formatFound ? WavError::kMissingData : WavError::kMissingFormat;
        return false;
    }
    if (info_.spec.channels != 1 || info_.spec.bitsPerSample != 16) {
        error_ = WavError::kUnsupportedFormat;
        return false;
    }
    if (!file.seek(info_.dataOffset)) {
        error_ = WavError::kTruncated;
        return false;
    }
    error_ = WavError::kNone;
    file_ = file;
    bytesRead_ = 0;
    return true;
}

std::size_t WavReader::readSamples(std::int16_t* destination,
                                   std::size_t sampleCapacity)
{
    if (!file_ || destination == nullptr || finished()) {
        return 0;
    }
    const std::uint32_t remaining = info_.dataSize - bytesRead_;
    std::size_t bytes = sampleCapacity * sizeof(std::int16_t);
    if (bytes > remaining) {
        bytes = remaining;
    }
    bytes &= ~static_cast<std::size_t>(1);
    const std::size_t read =
        file_.read(reinterpret_cast<std::uint8_t*>(destination), bytes);
    bytesRead_ += read;
    return read / sizeof(std::int16_t);
}

void WavReader::end()
{
    if (file_) {
        file_.close();
    }
    file_ = File();
    bytesRead_ = 0;
}

bool WavReader::isOpen() const
{
    return static_cast<bool>(file_);
}

bool WavReader::finished() const
{
    return !file_ || bytesRead_ >= info_.dataSize;
}

std::uint32_t WavReader::bytesRead() const
{
    return bytesRead_;
}

const WavInfo& WavReader::info() const
{
    return info_;
}

WavError WavReader::error() const
{
    return error_;
}

}  // namespace cardputer_recorder
