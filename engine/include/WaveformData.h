#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace moon::engine
{
struct WaveformBucket
{
    float minValue{0.0f};
    float maxValue{0.0f};
    float rms{0.0f};
};

struct WaveformMipLevel
{
    int samplesPerBucket{16};
    std::vector<WaveformBucket> buckets;
};

struct WaveformData
{
    int sampleRate{44100};
    int channelCount{0};
    std::uint64_t totalSamples{0};
    double durationSec{0.0};
    std::vector<WaveformMipLevel> mipLevels;

    const WaveformMipLevel& bestLevelForSamplesPerPixel(double samplesPerPixel) const noexcept
    {
        if (mipLevels.empty())
        {
            static const WaveformMipLevel emptyLevel{};
            return emptyLevel;
        }

        const auto targetSamplesPerBucket = std::max(1.0, samplesPerPixel);
        const WaveformMipLevel* bestLevel = &mipLevels.front();
        for (const auto& level : mipLevels)
        {
            if (static_cast<double>(level.samplesPerBucket) <= targetSamplesPerBucket)
            {
                bestLevel = &level;
            }
            else
            {
                break;
            }
        }

        return *bestLevel;
    }
};

using WaveformDataPtr = std::shared_ptr<const WaveformData>;
}
