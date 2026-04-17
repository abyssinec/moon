#pragma once

#include <memory>
#include <string>

#include "Logger.h"
#include "ProjectState.h"
#include "TracktionSyncBridge.h"

namespace moon::engine
{
enum class TimelineBackendMode
{
    Lightweight,
    TracktionHybrid
};

class ArrangementBackend
{
public:
    virtual ~ArrangementBackend() = default;

    virtual TimelineBackendMode mode() const noexcept = 0;
    virtual std::string backendSummary() const = 0;
    virtual std::string ensureTrack(ProjectState& state, const std::string& name) = 0;
    virtual std::string insertAudioClip(ProjectState& state,
                                        const std::string& trackId,
                                        const std::string& audioPath,
                                        double startSec,
                                        double durationSec) = 0;
    virtual std::string insertGeneratedClip(ProjectState& state,
                                            const std::string& trackId,
                                            const std::string& audioPath,
                                            double startSec,
                                            double durationSec,
                                            const std::string& sourceClipId,
                                            const std::string& modelName,
                                            const std::string& prompt,
                                            const std::string& cacheKey) = 0;
    virtual bool moveClip(ProjectState& state, const std::string& clipId, double newStartSec) = 0;
    virtual bool toggleTrackMute(ProjectState& state, const std::string& trackId) = 0;
    virtual bool toggleTrackSolo(ProjectState& state, const std::string& trackId) = 0;
};

std::unique_ptr<ArrangementBackend> createArrangementBackend(Logger& logger, TimelineBackendMode preferredMode);
bool tracktionTimelineBackendCompiled() noexcept;
}
