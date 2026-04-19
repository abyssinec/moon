#include "ArrangementBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace moon::engine
{
namespace
{
std::string makeTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

class LightweightArrangementBackend final : public ArrangementBackend
{
public:
    explicit LightweightArrangementBackend(Logger& logger)
        : logger_(logger)
    {
    }

    TimelineBackendMode mode() const noexcept override
    {
        return TimelineBackendMode::Lightweight;
    }

    std::string backendSummary() const override
    {
        return "lightweight";
    }

    std::string ensureTrack(ProjectState& state, const std::string& name) override
    {
        syncCounters(state);
        for (const auto& track : state.tracks)
        {
            if (track.name == name)
            {
                return track.id;
            }
        }

        TrackInfo track;
        track.id = "track-" + std::to_string(nextTrackId_++);
        track.name = name;
        state.tracks.push_back(track);
        logger_.info("Created track " + track.id);
        return track.id;
    }

    std::string insertAudioClip(ProjectState& state,
                                const std::string& trackId,
                                const std::string& audioPath,
                                double startSec,
                                double durationSec) override
    {
        syncCounters(state);
        AssetInfo asset;
        asset.id = "asset-" + std::to_string(nextAssetId_++);
        asset.path = audioPath;
        asset.generated = false;
        state.sourceAssets.emplace(asset.id, asset);

        ClipInfo clip;
        clip.id = "clip-" + std::to_string(nextClipId_++);
        clip.trackId = trackId;
        clip.assetId = asset.id;
        clip.startSec = startSec;
        clip.durationSec = durationSec;
        state.clips.push_back(clip);
        selectClip(state, clip.id);
        logger_.info("Inserted clip " + clip.id + " on " + trackId);
        return clip.id;
    }

    std::string insertGeneratedClip(ProjectState& state,
                                    const std::string& trackId,
                                    const std::string& audioPath,
                                    double startSec,
                                    double durationSec,
                                    const std::string& sourceClipId,
                                    const std::string& modelName,
                                    const std::string& prompt,
                                    const std::string& cacheKey) override
    {
        syncCounters(state);
        AssetInfo asset;
        asset.id = "asset-" + std::to_string(nextAssetId_++);
        asset.path = audioPath;
        asset.generated = true;
        asset.sourceClipId = sourceClipId;
        asset.modelName = modelName;
        asset.prompt = prompt;
        asset.createdAt = makeTimestamp();
        asset.cacheKey = cacheKey;
        state.generatedAssets.emplace(asset.id, asset);

        ClipInfo clip;
        clip.id = "clip-" + std::to_string(nextClipId_++);
        clip.trackId = trackId;
        clip.assetId = asset.id;
        clip.startSec = startSec;
        clip.durationSec = durationSec;
        state.clips.push_back(clip);
        selectClip(state, clip.id);
        logger_.info("Inserted generated clip " + clip.id + " on " + trackId);
        return clip.id;
    }

    bool moveClip(ProjectState& state, const std::string& clipId, double newStartSec) override
    {
        for (auto& clip : state.clips)
        {
            if (clip.id != clipId)
            {
                continue;
            }

            const auto clampedStartSec = std::max(0.0, newStartSec);
            if (std::abs(clip.startSec - clampedStartSec) < 0.0001)
            {
                return false;
            }

            clip.startSec = clampedStartSec;
            logger_.info("Moved clip " + clip.id + " to " + std::to_string(clampedStartSec) + " sec");
            return true;
        }

        return false;
    }

    bool moveClipToTrack(ProjectState& state, const std::string& clipId, const std::string& trackId, double newStartSec) override
    {
        for (auto& clip : state.clips)
        {
            if (clip.id != clipId)
            {
                continue;
            }

            const auto clampedStartSec = std::max(0.0, newStartSec);
            const bool trackChanged = clip.trackId != trackId;
            const bool timeChanged = std::abs(clip.startSec - clampedStartSec) >= 0.0001;
            if (!trackChanged && !timeChanged)
            {
                return false;
            }

            clip.trackId = trackId;
            clip.startSec = clampedStartSec;
            state.uiState.selectedTrackId = trackId;
            logger_.info("Moved clip " + clip.id + " to " + trackId + " at " + std::to_string(clampedStartSec) + " sec");
            return true;
        }

        return false;
    }

    bool toggleTrackMute(ProjectState& state, const std::string& trackId) override
    {
        for (auto& track : state.tracks)
        {
            if (track.id != trackId)
            {
                continue;
            }

            track.mute = !track.mute;
            if (track.mute && track.solo)
            {
                track.solo = false;
            }
            logger_.info("Track " + track.id + (track.mute ? " muted" : " unmuted"));
            return true;
        }

        return false;
    }

    bool toggleTrackSolo(ProjectState& state, const std::string& trackId) override
    {
        for (auto& track : state.tracks)
        {
            if (track.id != trackId)
            {
                continue;
            }

            track.solo = !track.solo;
            if (track.solo && track.mute)
            {
                track.mute = false;
            }
            logger_.info("Track " + track.id + (track.solo ? " soloed" : " unsoloed"));
            return true;
        }

        return false;
    }

private:
    static bool selectClip(ProjectState& state, const std::string& clipId)
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

    void syncCounters(const ProjectState& state)
    {
        for (const auto& track : state.tracks)
        {
            if (track.id.rfind("track-", 0) == 0)
            {
                nextTrackId_ = std::max(nextTrackId_, std::stoi(track.id.substr(6)) + 1);
            }
        }

        for (const auto& clip : state.clips)
        {
            if (clip.id.rfind("clip-", 0) == 0)
            {
                nextClipId_ = std::max(nextClipId_, std::stoi(clip.id.substr(5)) + 1);
            }
        }

        for (const auto& [id, _] : state.sourceAssets)
        {
            if (id.rfind("asset-", 0) == 0)
            {
                nextAssetId_ = std::max(nextAssetId_, std::stoi(id.substr(6)) + 1);
            }
        }

        for (const auto& [id, _] : state.generatedAssets)
        {
            if (id.rfind("asset-", 0) == 0)
            {
                nextAssetId_ = std::max(nextAssetId_, std::stoi(id.substr(6)) + 1);
            }
        }
    }

    Logger& logger_;
    int nextTrackId_{1};
    int nextClipId_{1};
    int nextAssetId_{1};
};

class TracktionHybridArrangementBackend final : public ArrangementBackend
{
public:
    explicit TracktionHybridArrangementBackend(Logger& logger)
        : logger_(logger)
        , syncBridge_(std::make_unique<TracktionSyncBridge>(logger))
        , fallback_(std::make_unique<LightweightArrangementBackend>(logger))
    {
#if MOON_HAS_TRACKTION
        logger_.info("Tracktion hybrid arrangement backend enabled");
#else
        logger_.warning("Tracktion hybrid arrangement backend requested but Tracktion is not compiled in; using lightweight fallback");
#endif
    }

    TimelineBackendMode mode() const noexcept override
    {
        return TimelineBackendMode::TracktionHybrid;
    }

    std::string backendSummary() const override
    {
#if MOON_HAS_TRACKTION
        return "tracktion-hybrid (lightweight edit graph fallback)";
#else
        return "tracktion-hybrid requested -> lightweight fallback";
#endif
    }

    std::string ensureTrack(ProjectState& state, const std::string& name) override
    {
        markDeferredSync("ensureTrack");
        syncBridge_->syncEditState(state, "ensureTrack");
        return fallback_->ensureTrack(state, name);
    }

    std::string insertAudioClip(ProjectState& state,
                                const std::string& trackId,
                                const std::string& audioPath,
                                double startSec,
                                double durationSec) override
    {
        markDeferredSync("insertAudioClip");
        syncBridge_->syncEditState(state, "insertAudioClip");
        return fallback_->insertAudioClip(state, trackId, audioPath, startSec, durationSec);
    }

    std::string insertGeneratedClip(ProjectState& state,
                                    const std::string& trackId,
                                    const std::string& audioPath,
                                    double startSec,
                                    double durationSec,
                                    const std::string& sourceClipId,
                                    const std::string& modelName,
                                    const std::string& prompt,
                                    const std::string& cacheKey) override
    {
        markDeferredSync("insertGeneratedClip");
        syncBridge_->syncEditState(state, "insertGeneratedClip");
        return fallback_->insertGeneratedClip(state, trackId, audioPath, startSec, durationSec, sourceClipId, modelName, prompt, cacheKey);
    }

    bool moveClip(ProjectState& state, const std::string& clipId, double newStartSec) override
    {
        markDeferredSync("moveClip");
        syncBridge_->syncEditState(state, "moveClip");
        return fallback_->moveClip(state, clipId, newStartSec);
    }

    bool moveClipToTrack(ProjectState& state, const std::string& clipId, const std::string& trackId, double newStartSec) override
    {
        markDeferredSync("moveClipToTrack");
        syncBridge_->syncEditState(state, "moveClipToTrack");
        return fallback_->moveClipToTrack(state, clipId, trackId, newStartSec);
    }

    bool toggleTrackMute(ProjectState& state, const std::string& trackId) override
    {
        markDeferredSync("toggleTrackMute");
        syncBridge_->syncEditState(state, "toggleTrackMute");
        return fallback_->toggleTrackMute(state, trackId);
    }

    bool toggleTrackSolo(ProjectState& state, const std::string& trackId) override
    {
        markDeferredSync("toggleTrackSolo");
        syncBridge_->syncEditState(state, "toggleTrackSolo");
        return fallback_->toggleTrackSolo(state, trackId);
    }

private:
    void markDeferredSync(const std::string& operation)
    {
        logger_.info("Tracktion hybrid seam handled " + operation + " via lightweight fallback path");
    }

    Logger& logger_;
    std::unique_ptr<TracktionSyncBridge> syncBridge_;
    std::unique_ptr<LightweightArrangementBackend> fallback_;
};
}

std::unique_ptr<ArrangementBackend> createArrangementBackend(Logger& logger, TimelineBackendMode preferredMode)
{
    if (preferredMode == TimelineBackendMode::TracktionHybrid)
    {
        return std::make_unique<TracktionHybridArrangementBackend>(logger);
    }

    return std::make_unique<LightweightArrangementBackend>(logger);
}

bool tracktionTimelineBackendCompiled() noexcept
{
#if MOON_HAS_TRACKTION
    return true;
#else
    return false;
#endif
}
}
