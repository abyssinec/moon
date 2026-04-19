#include "WaveformAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace moon::engine
{
namespace
{
constexpr int kAnalysisBlockSize = 32768;
constexpr int kBaseSamplesPerBucket = 16;
constexpr int kMipFactor = 4;

WaveformMipLevel buildNextMipLevel(const WaveformMipLevel& previousLevel)
{
    WaveformMipLevel nextLevel;
    nextLevel.samplesPerBucket = previousLevel.samplesPerBucket * kMipFactor;
    nextLevel.buckets.reserve((previousLevel.buckets.size() + kMipFactor - 1) / kMipFactor);

    for (std::size_t index = 0; index < previousLevel.buckets.size(); index += kMipFactor)
    {
        WaveformBucket combined{};
        combined.minValue = 1.0f;
        combined.maxValue = -1.0f;
        double rmsSquared = 0.0;
        int bucketCount = 0;

        for (std::size_t child = index; child < std::min(previousLevel.buckets.size(), index + static_cast<std::size_t>(kMipFactor)); ++child)
        {
            combined.minValue = std::min(combined.minValue, previousLevel.buckets[child].minValue);
            combined.maxValue = std::max(combined.maxValue, previousLevel.buckets[child].maxValue);
            rmsSquared += static_cast<double>(previousLevel.buckets[child].rms) * static_cast<double>(previousLevel.buckets[child].rms);
            ++bucketCount;
        }

        if (bucketCount == 0)
        {
            continue;
        }

        if (combined.maxValue < combined.minValue)
        {
            combined.minValue = 0.0f;
            combined.maxValue = 0.0f;
        }

        combined.rms = static_cast<float>(std::sqrt(rmsSquared / static_cast<double>(bucketCount)));
        nextLevel.buckets.push_back(combined);
    }

    return nextLevel;
}
}

WaveformAnalyzer::WaveformAnalyzer(Logger& logger)
    : logger_(logger)
{
#if MOON_HAS_JUCE
    formatManager_.registerBasicFormats();
#endif
}

WaveformDataPtr WaveformAnalyzer::analyzeFile(const std::string& path) const
{
    auto waveform = std::make_shared<WaveformData>();

#if !MOON_HAS_JUCE
    (void) path;
    return waveform;
#else
    const juce::File file(path);
    if (!file.existsAsFile())
    {
        logger_.error("WaveformAnalyzer: source file missing: " + path);
        return waveform;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr)
    {
        logger_.error("WaveformAnalyzer: unable to create audio reader for " + path);
        return waveform;
    }

    waveform->sampleRate = static_cast<int>(reader->sampleRate);
    waveform->channelCount = static_cast<int>(reader->numChannels);
    waveform->totalSamples = static_cast<std::uint64_t>(juce::jmax<juce::int64>(0, reader->lengthInSamples));
    waveform->durationSec = waveform->sampleRate > 0
        ? static_cast<double>(waveform->totalSamples) / static_cast<double>(waveform->sampleRate)
        : 0.0;

    WaveformMipLevel baseLevel;
    baseLevel.samplesPerBucket = kBaseSamplesPerBucket;
    baseLevel.buckets.reserve(static_cast<std::size_t>((waveform->totalSamples + kBaseSamplesPerBucket - 1) / kBaseSamplesPerBucket));

    juce::AudioBuffer<float> readBuffer(static_cast<int>(reader->numChannels), kAnalysisBlockSize);
    float bucketMin = 1.0f;
    float bucketMax = -1.0f;
    double bucketSquared = 0.0;
    int bucketSampleCount = 0;

    for (juce::int64 startSample = 0; startSample < reader->lengthInSamples; startSample += kAnalysisBlockSize)
    {
        const auto samplesToRead = static_cast<int>(std::min<juce::int64>(kAnalysisBlockSize, reader->lengthInSamples - startSample));
        readBuffer.clear();
        if (!reader->read(&readBuffer, 0, samplesToRead, startSample, true, reader->numChannels > 1))
        {
            logger_.warning("WaveformAnalyzer: partial read failure for " + path);
            break;
        }

        for (int sample = 0; sample < samplesToRead; ++sample)
        {
            float sampleMin = 1.0f;
            float sampleMax = -1.0f;
            double sampleSquared = 0.0;
            int contributingChannels = 0;

            for (int channel = 0; channel < static_cast<int>(reader->numChannels); ++channel)
            {
                const auto value = readBuffer.getSample(channel, sample);
                sampleMin = std::min(sampleMin, value);
                sampleMax = std::max(sampleMax, value);
                sampleSquared += static_cast<double>(value) * static_cast<double>(value);
                ++contributingChannels;
            }

            if (contributingChannels == 0)
            {
                continue;
            }

            bucketMin = std::min(bucketMin, sampleMin);
            bucketMax = std::max(bucketMax, sampleMax);
            bucketSquared += sampleSquared / static_cast<double>(contributingChannels);
            ++bucketSampleCount;

            if (bucketSampleCount >= kBaseSamplesPerBucket)
            {
                baseLevel.buckets.push_back({
                    bucketMin,
                    bucketMax,
                    static_cast<float>(std::sqrt(bucketSquared / static_cast<double>(bucketSampleCount)))
                });
                bucketMin = 1.0f;
                bucketMax = -1.0f;
                bucketSquared = 0.0;
                bucketSampleCount = 0;
            }
        }
    }

    if (bucketSampleCount > 0)
    {
        baseLevel.buckets.push_back({
            bucketMin == 1.0f ? 0.0f : bucketMin,
            bucketMax == -1.0f ? 0.0f : bucketMax,
            static_cast<float>(std::sqrt(bucketSquared / static_cast<double>(bucketSampleCount)))
        });
    }

    waveform->mipLevels.push_back(std::move(baseLevel));
    while (!waveform->mipLevels.empty())
    {
        const auto& previousLevel = waveform->mipLevels.back();
        if (previousLevel.buckets.size() <= 256)
        {
            break;
        }

        waveform->mipLevels.push_back(buildNextMipLevel(previousLevel));
    }

    logger_.info("WaveformAnalyzer: built " + std::to_string(waveform->mipLevels.size()) + " mip levels for " + path);
    return waveform;
#endif
}
}
