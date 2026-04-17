#include "WaveformService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace moon::engine
{
WaveformService::WaveformService(Logger& logger)
    : logger_(logger)
{
}

void WaveformService::markRequested(const std::string& path)
{
    ready_.try_emplace(path, loadWavData(path));
    logger_.info("Waveform marked ready for " + path);
}

const WaveformData& WaveformService::requestWaveform(const std::string& path)
{
    const auto [it, inserted] = ready_.try_emplace(path, loadWavData(path));
    if (inserted)
    {
        logger_.info("Loaded waveform for " + path);
    }
    return it->second;
}

bool WaveformService::hasWaveform(const std::string& path) const
{
    return ready_.find(path) != ready_.end();
}

std::optional<WaveformData> WaveformService::tryGetWaveform(const std::string& path) const
{
    const auto it = ready_.find(path);
    if (it == ready_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

double WaveformService::durationFor(const std::string& path)
{
    return requestWaveform(path).durationSec;
}

WaveformData WaveformService::loadWavData(const std::string& path)
{
    WaveformData result;
    result.peaks.assign(64, 0.0f);

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        logger_.error("Unable to open waveform source: " + path);
        return result;
    }

    std::array<char, 4> chunkId{};
    in.read(chunkId.data(), 4);
    if (std::string(chunkId.data(), 4) != "RIFF")
    {
        logger_.error("Unsupported audio format (expected RIFF/WAV): " + path);
        return result;
    }

    in.seekg(22, std::ios::beg);
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 44100;
    std::uint16_t bitsPerSample = 16;
    in.read(reinterpret_cast<char*>(&channels), sizeof(channels));
    in.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
    in.seekg(34, std::ios::beg);
    in.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));

    in.seekg(12, std::ios::beg);
    std::uint32_t dataSize = 0;
    while (in && !in.eof())
    {
        std::array<char, 4> id{};
        std::uint32_t size = 0;
        in.read(id.data(), 4);
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (!in)
        {
            break;
        }

        if (std::string(id.data(), 4) == "data")
        {
            dataSize = size;
            break;
        }

        in.seekg(size, std::ios::cur);
    }

    if (dataSize == 0 || bitsPerSample != 16 || channels == 0)
    {
        logger_.error("Unsupported or empty WAV payload: " + path);
        return result;
    }

    result.sampleRate = static_cast<int>(sampleRate);
    result.channelCount = static_cast<int>(channels);
    const std::uint32_t bytesPerFrame = channels * sizeof(std::int16_t);
    const std::uint32_t frameCount = dataSize / bytesPerFrame;
    result.durationSec = frameCount > 0 ? static_cast<double>(frameCount) / static_cast<double>(sampleRate) : 0.0;

    std::vector<std::int16_t> samples(frameCount * channels);
    in.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(dataSize));
    if (!in)
    {
        logger_.error("Failed to read sample data from: " + path);
        return result;
    }

    const std::size_t bucketCount = result.peaks.size();
    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        float maxValue = 0.0f;
        for (std::uint16_t channel = 0; channel < channels; ++channel)
        {
            const auto sample = samples[frame * channels + channel];
            maxValue = std::max(maxValue, std::abs(static_cast<float>(sample) / 32768.0f));
        }

        const auto bucket = std::min<std::size_t>(bucketCount - 1, (static_cast<std::size_t>(frame) * bucketCount) / std::max<std::uint32_t>(1, frameCount));
        result.peaks[bucket] = std::max(result.peaks[bucket], maxValue);
    }

    return result;
}
}
