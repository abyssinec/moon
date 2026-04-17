#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Logger.h"
#include "ProjectState.h"

namespace moon::engine
{
class ExportService
{
public:
    explicit ExportService(Logger& logger);
    bool exportMix(const ProjectState& state, const std::filesystem::path& outputPath);
    bool exportRegion(const ProjectState& state,
                      double startSec,
                      double endSec,
                      const std::filesystem::path& outputPath);
    bool exportStemTracks(const ProjectState& state, const std::filesystem::path& outputDirectory);
    double estimateMixDuration(const ProjectState& state) const;

private:
    bool writePcm16Wav(const std::filesystem::path& outputPath,
                       const std::vector<float>& interleavedSamples,
                       int sampleRate,
                       int channelCount) const;
    bool renderProjectRange(const ProjectState& state,
                            double startSec,
                            double endSec,
                            const std::optional<std::string>& trackId,
                            const std::filesystem::path& outputPath,
                            std::size_t* renderedClipCount = nullptr) const;
    static bool clipParticipatesInMix(const ProjectState& state, const ClipInfo& clip);
    static bool clipIntersectsRegion(const ClipInfo& clip, double startSec, double endSec);
    static double mixDuration(const ProjectState& state);
    static double trackDuration(const ProjectState& state, const std::string& trackId);

    Logger& logger_;
};
}
