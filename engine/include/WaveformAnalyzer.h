#pragma once

#include "Logger.h"
#include "WaveformData.h"

#include <string>

#if MOON_HAS_JUCE
#include <juce_audio_formats/juce_audio_formats.h>
#endif

namespace moon::engine
{
class WaveformAnalyzer
{
public:
    explicit WaveformAnalyzer(Logger& logger);
    WaveformDataPtr analyzeFile(const std::string& path) const;

private:
    Logger& logger_;
#if MOON_HAS_JUCE
    mutable juce::AudioFormatManager formatManager_;
#endif
};
}
