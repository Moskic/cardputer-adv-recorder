#include <unity.h>

#include <array>
#include <cstring>
#include <vector>

#include "recorder/media/file_naming.h"
#include "recorder/media/wav_format.h"

using namespace cardputer_recorder;

void test_header_round_trip()
{
    std::vector<std::uint8_t> wav(kPcmWavHeaderSize + 32000);
    encodePcmWavHeader(wav.data(), {16000, 1, 16}, 32000);
    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kNone),
        static_cast<int>(parseWav(wav.data(), wav.size(), info)));
    TEST_ASSERT_EQUAL_UINT32(16000, info.spec.sampleRate);
    TEST_ASSERT_EQUAL_UINT16(1, info.spec.channels);
    TEST_ASSERT_EQUAL_UINT16(16, info.spec.bitsPerSample);
    TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
    TEST_ASSERT_EQUAL_UINT32(32000, info.dataSize);
}

void test_fixed_header_can_be_verified_against_file_size()
{
    std::array<std::uint8_t, kPcmWavHeaderSize> header{};
    encodePcmWavHeader(header.data(), {16000, 1, 16}, 32000);
    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kNone),
        static_cast<int>(parsePcmWavHeader(
            header.data(), header.size(), 32044, info)));
    TEST_ASSERT_EQUAL_UINT32(32000, info.dataSize);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kTruncated),
        static_cast<int>(parsePcmWavHeader(
            header.data(), header.size(), 32043, info)));
}

void test_streaming_header_uses_actual_file_size()
{
    std::array<std::uint8_t, kPcmWavHeaderSize> header{};
    encodeStreamingPcmWavHeader(
        header.data(), {16000, 1, 16});
    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kNone),
        static_cast<int>(parsePcmWavHeader(
            header.data(), header.size(), 64044, info)));
    TEST_ASSERT_EQUAL_UINT32(64000, info.dataSize);
}

void test_unknown_chunk_is_skipped()
{
    std::array<std::uint8_t, kPcmWavHeaderSize> header{};
    encodePcmWavHeader(header.data(), {16000, 1, 16}, 0);
    std::vector<std::uint8_t> wav;
    wav.insert(wav.end(), header.begin(), header.begin() + 36);
    const std::uint8_t junk[] = {
        'J', 'U', 'N', 'K', 3, 0, 0, 0, 1, 2, 3, 0,
    };
    wav.insert(wav.end(), std::begin(junk), std::end(junk));
    wav.insert(wav.end(), header.begin() + 36, header.end());

    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kNone),
        static_cast<int>(parseWav(wav.data(), wav.size(), info)));
    TEST_ASSERT_EQUAL_UINT32(56, info.dataOffset);
}

void test_truncated_chunk_is_rejected()
{
    const std::uint8_t wav[] = {
        'R', 'I', 'F', 'F', 20, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0,
    };
    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kTruncated),
        static_cast<int>(parseWav(wav, sizeof(wav), info)));
}

void test_unsupported_encoding_is_rejected()
{
    std::array<std::uint8_t, kPcmWavHeaderSize> header{};
    encodePcmWavHeader(header.data(), {16000, 1, 16}, 0);
    header[20] = 3;
    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kUnsupportedFormat),
        static_cast<int>(parseWav(header.data(), header.size(), info)));
}

void test_recording_name()
{
    char path[16];
    TEST_ASSERT_TRUE(makeRecordingPath(42, path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/REC0042.WAV", path);
    TEST_ASSERT_TRUE(makeRecordingPath(1, path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/REC0001.WAV", path);
    TEST_ASSERT_TRUE(makeRecordingPath(9999, path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("/REC9999.WAV", path);
    TEST_ASSERT_FALSE(makeRecordingPath(10000, path, sizeof(path)));
    TEST_ASSERT_FALSE(makeRecordingPath(1, path, 8));
    TEST_ASSERT_FALSE(makeRecordingPath(1, nullptr, sizeof(path)));
}

void test_missing_data_chunk_is_rejected()
{
    const std::uint8_t wav[] = {
        'R', 'I', 'F', 'F', 28, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x80, 0x3e, 0, 0, 0, 0x7d, 0, 0, 2, 0, 16, 0,
    };
    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kMissingData),
        static_cast<int>(parseWav(wav, sizeof(wav), info)));
}

void test_missing_format_chunk_is_rejected()
{
    const std::uint8_t wav[] = {
        'R', 'I', 'F', 'F', 12, 0, 0, 0, 'W', 'A', 'V', 'E',
        'd', 'a', 't', 'a', 0, 0, 0, 0,
    };
    WavInfo info;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WavError::kMissingFormat),
        static_cast<int>(parseWav(wav, sizeof(wav), info)));
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_round_trip);
    RUN_TEST(test_fixed_header_can_be_verified_against_file_size);
    RUN_TEST(test_streaming_header_uses_actual_file_size);
    RUN_TEST(test_unknown_chunk_is_skipped);
    RUN_TEST(test_truncated_chunk_is_rejected);
    RUN_TEST(test_unsupported_encoding_is_rejected);
    RUN_TEST(test_recording_name);
    RUN_TEST(test_missing_data_chunk_is_rejected);
    RUN_TEST(test_missing_format_chunk_is_rejected);
    return UNITY_END();
}
