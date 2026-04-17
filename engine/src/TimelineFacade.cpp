#include "TimelineFacade.h"

#include <algorithm>

namespace moon::engine
{
namespace
{
bool selectClipInternal(ProjectState& state, const std::string& clipId)
{
    bool found = false;
    for (auto& clip : state.clips)
    {
        clip.selected = (clip.id == clipId);
        found = found || clip.selected;
        if (clip.selected)
        {
            state.uiState.selectedClipId = clip.id;
        }
    }
    return found;
}
}

TimelineFacade::TimelineFacade(Logger& logger)
    : logger_(logger)
    , backend_(createArrangementBackend(logger_, preferredBackendMode_))
{
}

std::string TimelineFacade::ensureTrack(ProjectState& state, const std::string& name)
{
    return backend_->ensureTrack(state, name);
}

std::string TimelineFacade::insertAudioClip(ProjectState& state,
                                            const std::string& trackId,
                                            const std::string& audioPath,
                                            double startSec,
                                            double durationSec)
{
    return backend_->insertAudioClip(state, trackId, audioPath, startSec, durationSec);
}

std::string TimelineFacade::insertGeneratedClip(ProjectState& state,
                                                const std::string& trackId,
                                                const std::string& audioPath,
                                                double startSec,
                                                double durationSec,
                                                const std::string& sourceClipId,
                                                const std::string& modelName,
                                                const std::string& prompt,
                                                const std::string& cacheKey)
{
    return backend_->insertGeneratedClip(
        state,
        trackId,
        audioPath,
        startSec,
        durationSec,
        sourceClipId,
        modelName,
        prompt,
        cacheKey);
}

bool TimelineFacade::selectClip(ProjectState& state, const std::string& clipId)
{
    return selectClipInternal(state, clipId);
}

bool TimelineFacade::selectTrack(ProjectState& state, const std::string& trackId)
{
    for (const auto& track : state.tracks)
    {
        if (track.id == trackId)
        {
            state.uiState.selectedTrackId = trackId;
            return true;
        }
    }
    return false;
}

void TimelineFacade::setSelectedRegion(ProjectState& state, double startSec, double endSec)
{
    state.uiState.hasSelectedRegion = true;
    state.uiState.selectedRegionStartSec = std::min(startSec, endSec);
    state.uiState.selectedRegionEndSec = std::max(startSec, endSec);
}

void TimelineFacade::clearSelectedRegion(ProjectState& state)
{
    state.uiState.hasSelectedRegion = false;
    state.uiState.selectedRegionStartSec = 0.0;
    state.uiState.selectedRegionEndSec = 0.0;
}

bool TimelineFacade::moveClip(ProjectState& state, const std::string& clipId, double newStartSec)
{
    return backend_->moveClip(state, clipId, newStartSec);
}

bool TimelineFacade::toggleTrackMute(ProjectState& state, const std::string& trackId)
{
    return backend_->toggleTrackMute(state, trackId);
}

bool TimelineFacade::toggleTrackSolo(ProjectState& state, const std::string& trackId)
{
    return backend_->toggleTrackSolo(state, trackId);
}

void TimelineFacade::setPreferredBackend(TimelineBackendMode mode)
{
    preferredBackendMode_ = mode;
    backend_ = createArrangementBackend(logger_, preferredBackendMode_);
    logger_.info("Timeline backend set to " + backend_->backendSummary());
}

TimelineBackendMode TimelineFacade::activeBackendMode() const noexcept
{
    return backend_ != nullptr ? backend_->mode() : TimelineBackendMode::Lightweight;
}

bool TimelineFacade::tracktionBackendCompiled() const noexcept
{
    return tracktionTimelineBackendCompiled();
}

std::string TimelineFacade::backendSummary() const
{
    return backend_ != nullptr ? backend_->backendSummary() : "lightweight";
}
}
