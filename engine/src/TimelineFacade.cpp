#include "TimelineFacade.h"

#include <algorithm>
#include <unordered_set>

namespace moon::engine
{
namespace
{
void clearSelectedRegionInternal(ProjectState& state)
{
    state.uiState.hasSelectedRegion = false;
    state.uiState.selectedRegionStartSec = 0.0;
    state.uiState.selectedRegionEndSec = 0.0;
    state.uiState.selectedRegionStartTrackIndex = -1;
    state.uiState.selectedRegionEndTrackIndex = -1;
}

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
            state.uiState.selectedTrackId = clip.trackId;
        }
    }

    if (found)
    {
        clearSelectedRegionInternal(state);
    }
    else
    {
        state.uiState.selectedClipId.clear();
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
            state.uiState.selectedClipId.clear();
            for (auto& clip : state.clips)
            {
                clip.selected = false;
            }
            return true;
        }
    }
    return false;
}

void TimelineFacade::setSelectedRegion(ProjectState& state, double startSec, double endSec)
{
    int trackIndex = -1;
    if (!state.uiState.selectedTrackId.empty())
    {
        for (std::size_t i = 0; i < state.tracks.size(); ++i)
        {
            if (state.tracks[i].id == state.uiState.selectedTrackId)
            {
                trackIndex = static_cast<int>(i);
                break;
            }
        }
    }

    setSelectedRegion(state, startSec, endSec, trackIndex, trackIndex);
}

void TimelineFacade::setSelectedRegion(ProjectState& state, double startSec, double endSec, int startTrackIndex, int endTrackIndex)
{
    state.uiState.hasSelectedRegion = true;
    state.uiState.selectedRegionStartSec = std::min(startSec, endSec);
    state.uiState.selectedRegionEndSec = std::max(startSec, endSec);
    state.uiState.selectedRegionStartTrackIndex = std::min(startTrackIndex, endTrackIndex);
    state.uiState.selectedRegionEndTrackIndex = std::max(startTrackIndex, endTrackIndex);
}

void TimelineFacade::clearSelectedRegion(ProjectState& state)
{
    clearSelectedRegionInternal(state);
}

bool TimelineFacade::moveClip(ProjectState& state, const std::string& clipId, double newStartSec)
{
    return backend_->moveClip(state, clipId, newStartSec);
}

bool TimelineFacade::moveClipToTrack(ProjectState& state, const std::string& clipId, const std::string& trackId, double newStartSec)
{
    return backend_->moveClipToTrack(state, clipId, trackId, newStartSec);
}

bool TimelineFacade::renameTrack(ProjectState& state, const std::string& trackId, const std::string& newName)
{
    for (auto& track : state.tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        if (track.name == newName)
        {
            return false;
        }

        track.name = newName;
        logger_.info("Renamed track " + track.id + " to " + newName);
        return true;
    }

    return false;
}

bool TimelineFacade::deleteTrack(ProjectState& state, const std::string& trackId)
{
    if (state.tracks.size() <= 1)
    {
        return false;
    }

    const auto trackIt = std::find_if(
        state.tracks.begin(),
        state.tracks.end(),
        [&trackId](const TrackInfo& track)
        {
            return track.id == trackId;
        });
    if (trackIt == state.tracks.end())
    {
        return false;
    }

    bool removedAnyClip = false;
    state.clips.erase(
        std::remove_if(
            state.clips.begin(),
            state.clips.end(),
            [&trackId, &removedAnyClip](const ClipInfo& clip)
            {
                if (clip.trackId == trackId)
                {
                    removedAnyClip = true;
                    return true;
                }
                return false;
            }),
        state.clips.end());

    state.tracks.erase(trackIt);

    std::unordered_set<std::string> usedAssetIds;
    for (const auto& clip : state.clips)
    {
        usedAssetIds.insert(clip.assetId);
    }

    for (auto it = state.sourceAssets.begin(); it != state.sourceAssets.end();)
    {
        if (!usedAssetIds.contains(it->first))
        {
            it = state.sourceAssets.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = state.generatedAssets.begin(); it != state.generatedAssets.end();)
    {
        if (!usedAssetIds.contains(it->first))
        {
            it = state.generatedAssets.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (state.uiState.selectedTrackId == trackId)
    {
        state.uiState.selectedTrackId = state.tracks.empty() ? std::string{} : state.tracks.front().id;
    }
    if (state.uiState.selectedClipId.empty()
        || std::none_of(state.clips.begin(), state.clips.end(), [&state](const ClipInfo& clip) { return clip.id == state.uiState.selectedClipId; }))
    {
        state.uiState.selectedClipId.clear();
        for (auto& clip : state.clips)
        {
            clip.selected = false;
        }
    }

    logger_.info("Deleted track " + trackId + (removedAnyClip ? " with clips" : ""));
    return true;
}

bool TimelineFacade::setTrackColor(ProjectState& state, const std::string& trackId, const std::string& colorHex)
{
    for (auto& track : state.tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        if (track.colorHex == colorHex)
        {
            return false;
        }

        track.colorHex = colorHex;
        logger_.info("Changed track color " + track.id + " to " + colorHex);
        return true;
    }

    return false;
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
