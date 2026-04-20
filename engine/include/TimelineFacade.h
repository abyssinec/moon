#pragma once

#include <memory>
#include <string>

#include "ArrangementBackend.h"
#include "Logger.h"
#include "ProjectState.h"

namespace moon::engine
{
class TimelineFacade
{
public:
    explicit TimelineFacade(Logger& logger);

    std::string ensureTrack(ProjectState& state, const std::string& name);
    std::string insertAudioClip(ProjectState& state,
                                const std::string& trackId,
                                const std::string& audioPath,
                                double startSec,
                                double durationSec);
    std::string insertGeneratedClip(ProjectState& state,
                                    const std::string& trackId,
                                    const std::string& audioPath,
                                    double startSec,
                                    double durationSec,
                                    const std::string& sourceClipId,
                                    const std::string& modelName,
                                    const std::string& prompt,
                                    const std::string& cacheKey);
    bool selectClip(ProjectState& state, const std::string& clipId);
    bool selectTrack(ProjectState& state, const std::string& trackId);
    void setSelectedRegion(ProjectState& state, double startSec, double endSec);
    void setSelectedRegion(ProjectState& state, double startSec, double endSec, int startTrackIndex, int endTrackIndex);
    void clearSelectedRegion(ProjectState& state);
    bool moveClip(ProjectState& state, const std::string& clipId, double newStartSec);
    bool moveClipToTrack(ProjectState& state, const std::string& clipId, const std::string& trackId, double newStartSec);
    bool renameTrack(ProjectState& state, const std::string& trackId, const std::string& newName);
    bool deleteTrack(ProjectState& state, const std::string& trackId);
    bool setTrackColor(ProjectState& state, const std::string& trackId, const std::string& colorHex);
    bool toggleTrackMute(ProjectState& state, const std::string& trackId);
    bool toggleTrackSolo(ProjectState& state, const std::string& trackId);
    void setPreferredBackend(TimelineBackendMode mode);
    TimelineBackendMode preferredBackendMode() const noexcept { return preferredBackendMode_; }
    TimelineBackendMode activeBackendMode() const noexcept;
    bool tracktionBackendCompiled() const noexcept;
    std::string backendSummary() const;

private:
    Logger& logger_;
    TimelineBackendMode preferredBackendMode_{TimelineBackendMode::Lightweight};
    std::unique_ptr<ArrangementBackend> backend_;
};
}
