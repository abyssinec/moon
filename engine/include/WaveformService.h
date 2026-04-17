#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Logger.h"

namespace moon::engine
{
struct WaveformData
{
    int sampleRate{44100};
    int channelCount{2};
    double durationSec{0.0};
    std::vector<float> peaks;
    std::vector<float> mins;
    std::vector<float> maxs;
};

class WaveformService
{
public:
    explicit WaveformService(Logger& logger);
    void markRequested(const std::string& path);
    const WaveformData& requestWaveform(const std::string& path);
    bool hasWaveform(const std::string& path) const;
    std::optional<WaveformData> tryGetWaveform(const std::string& path) const;
    double durationFor(const std::string& path);

private:
    WaveformData loadWavData(const std::string& path);

    Logger& logger_;
    std::unordered_map<std::string, WaveformData> ready_;
};
}
