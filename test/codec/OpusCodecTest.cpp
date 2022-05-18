#include "codec/AudioLevel.h"
#include "codec/OpusDecoder.h"
#include "codec/OpusEncoder.h"
#include "logger/Logger.h"
#include <cmath>
#include <gtest/gtest.h>

namespace codec
{
int computeAudioLevel(const int16_t* payload, int samples);
}

struct OpusTest : testing::Test
{
    static constexpr double sampleFrequency = 48000;
    static constexpr int32_t samples = 48000 / 50;

    void SetUp() override
    {
        for (int i = 0; i < samples; ++i)
        {
            _pcmData[i * 2] = sin(2 * PI * i * 400 / sampleFrequency) * amplitude;
            _pcmData[i * 2 + 1] = _pcmData[i * 2];
        }
    }

    static constexpr double PI = 3.14159;

    static constexpr double amplitude = 2000;
    int16_t _pcmData[samples * 2];
    uint8_t _opusData[samples];
};

TEST_F(OpusTest, encode)
{
    codec::OpusEncoder encoder;
    codec::OpusDecoder decoder;

    auto dB1 = codec::computeAudioLevel(_pcmData, samples);
    EXPECT_EQ(27, dB1);
    auto opusBytes = encoder.encode(_pcmData, samples, _opusData, samples);
    EXPECT_GT(opusBytes, 100);

    auto samplesProduced = decoder.decode(5,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));

    EXPECT_EQ(samplesProduced, samples * 1);
    auto dB2 = codec::computeAudioLevel(_pcmData, samples);
    EXPECT_EQ(31, dB2);
}

TEST_F(OpusTest, healing)
{
    codec::OpusEncoder encoder;
    codec::OpusDecoder decoder;

    auto opusBytes = encoder.encode(_pcmData, samples, _opusData, samples);
    EXPECT_GT(opusBytes, 100);

    auto samplesProduced = decoder.decode(5,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));

    EXPECT_EQ(samplesProduced, samples * 1);

    samplesProduced = decoder.decode(6,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));
    EXPECT_EQ(samplesProduced, samples * 1);
    samplesProduced = decoder.decode(7,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));
    EXPECT_EQ(samplesProduced, samples * 1);
    samplesProduced = decoder.decode(8,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));

    EXPECT_EQ(samplesProduced, samples * 1);

    auto dB = codec::computeAudioLevel(_pcmData, samples);
    EXPECT_EQ(static_cast<int>(-25), -dB);
}

TEST_F(OpusTest, seqSkip)
{
    codec::OpusEncoder encoder;
    codec::OpusDecoder decoder;

    auto opusBytes = encoder.encode(_pcmData, samples, _opusData, samples);
    EXPECT_GT(opusBytes, 100);

    auto samplesProduced = decoder.decode(5,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));

    EXPECT_EQ(samplesProduced, samples * 1);

    samplesProduced = decoder.decode(7,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));
    EXPECT_EQ(samplesProduced, samples * 1);

    samplesProduced = decoder.decode(8,
        _opusData,
        opusBytes,
        reinterpret_cast<unsigned char*>(_pcmData),
        samples * 2 * sizeof(int16_t));

    EXPECT_EQ(samplesProduced, samples * 1);

    auto dB = codec::computeAudioLevel(_pcmData, samples);
    EXPECT_EQ(static_cast<int>(-21), -dB);
}
