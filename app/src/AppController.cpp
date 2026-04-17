#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "AppConfig.h"

namespace moon::app
{
namespace
{
moon::engine::ClipInfo* findSelectedClip(moon::engine::ProjectState& state)
{
    for (auto& clip : state.clips)
    {
        if (clip.selected)
        {
            return &clip;
        }
    }
    return nullptr;
}

std::string resolveAssetPath(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip)
{
    if (const auto sourceIt = state.sourceAssets.find(clip.assetId); sourceIt != state.sourceAssets.end())
    {
        return sourceIt->second.path;
    }
    if (const auto generatedIt = state.generatedAssets.find(clip.assetId); generatedIt != state.generatedAssets.end())
    {
        return generatedIt->second.path;
    }
    return {};
}

void clearSelectedClipState(moon::engine::ProjectState& state)
{
    state.uiState.selectedClipId.clear();
    for (auto& clip : state.clips)
    {
        clip.selected = false;
    }
}
}

AppController::AppController()
    : logger_(std::make_unique<moon::engine::Logger>())
    , settingsService_(std::make_unique<moon::engine::SettingsService>())
    , settings_(settingsService_->load())
    , projectManager_(std::make_unique<moon::engine::ProjectManager>(*logger_))
    , runtimeCoordinator_(std::make_unique<moon::engine::EngineRuntimeCoordinator>(*logger_))
    , timeline_(std::make_unique<moon::engine::TimelineFacade>(*logger_))
    , clipOperations_(std::make_unique<moon::engine::ClipOperations>(*logger_))
    , transport_(std::make_unique<moon::engine::TransportFacade>(*logger_))
    , waveformService_(std::make_unique<moon::engine::WaveformService>(*logger_))
    , exportService_(std::make_unique<moon::engine::ExportService>(*logger_))
    , aiJobClient_(std::make_unique<moon::engine::AIJobClient>(settings_.backendUrl.empty() ? std::string(AppConfig::backendUrl) : settings_.backendUrl, *logger_))
    , taskManager_(std::make_unique<moon::engine::TaskManager>(*aiJobClient_, *logger_))
{
    transport_->setProjectState(&projectManager_->state());
#if MOON_HAS_TRACKTION
    timeline_->setPreferredBackend(moon::engine::TimelineBackendMode::TracktionHybrid);
    transport_->setPreferredBackend(moon::engine::TransportBackendMode::TracktionHybrid);
#endif
    syncEngineIntegrationState();
}

bool AppController::startup()
{
    logger_->info("AppController startup");
    backendHealth_ = aiJobClient_->healthCheck();
    backendModels_ = aiJobClient_->models();
    logger_->info("Backend health status: " + backendHealth_.status);
    backendFallbackNoticeActive_ = !aiJobClient_->backendReachable();
    autosaveRecoveryNoticeActive_ = false;
    refreshStartupNoticeState();
    if (!projectManager_->projectFilePath().has_value())
    {
        const auto defaultRoot = (std::filesystem::current_path() / "workspace_project").string();
        createProject("Untitled Project", defaultRoot);
        logger_->info("Created default startup project at " + defaultRoot);
        if (projectManager_->hasAutosave())
        {
            if (restoreAutosave(defaultRoot))
            {
                autosaveRecoveryNoticeActive_ = true;
                refreshStartupNoticeState();
            }
        }
    }
    return true;
}

bool AppController::createProject(const std::string& name, const std::string& rootPath)
{
    const auto created = projectManager_->createProject(name, rootPath);
    if (created)
    {
        projectManager_->state().sampleRate = settings_.defaultSampleRate;
        syncEngineIntegrationState();
        previewPlaybackActive_ = false;
        markPreviewPlaybackDirty();
        clearProjectDirty();
        saveProject();
    }
    return created;
}

bool AppController::openProject(const std::string& projectFilePath)
{
    const auto opened = projectManager_->openProject(projectFilePath);
    if (!opened)
    {
        return false;
    }

    auto& state = projectManager_->state();
    for (const auto& [_, asset] : state.sourceAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    for (const auto& [_, asset] : state.generatedAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    previewPlaybackActive_ = false;
    markPreviewPlaybackDirty();
    syncEngineIntegrationState();
    clearProjectDirty();
    syncTransportToSelection();
    return true;
}

bool AppController::restoreAutosave(const std::string& projectRootPath)
{
    const auto restored = projectManager_->restoreFromAutosave(projectRootPath);
    if (!restored)
    {
        return false;
    }

    auto& state = projectManager_->state();
    for (const auto& [_, asset] : state.sourceAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    for (const auto& [_, asset] : state.generatedAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    previewPlaybackActive_ = false;
    markPreviewPlaybackDirty();
    syncEngineIntegrationState();
    clearProjectDirty();
    syncTransportToSelection();
    return true;
}

bool AppController::saveProject()
{
    const auto saved = projectManager_->saveProject();
    if (saved)
    {
        clearProjectDirty();
    }
    return saved;
}

bool AppController::importAudio(const std::string& audioPath)
{
    if (!std::filesystem::exists(audioPath))
    {
        logger_->error("Import failed, file not found: " + audioPath);
        return false;
    }

    auto& state = projectManager_->state();
    if (state.tracks.empty())
    {
        timeline_->ensureTrack(state, "Track 1");
    }

    const auto durationSec = std::max(0.1, waveformService_->durationFor(audioPath));
    const auto clipId = timeline_->insertAudioClip(state, state.tracks.front().id, audioPath, 0.0, durationSec);
    markPreviewPlaybackDirty();
    markProjectDirty();
    syncTransportToSelection();
    logger_->info("Imported audio clip: " + clipId);
    return true;
}

std::optional<std::string> AppController::projectFilePath() const
{
    return projectManager_->projectFilePath();
}

std::string AppController::backendStatusSummary() const
{
    std::string summary = aiJobClient_->backendReachable() ? "Backend: live" : "Backend: stub fallback";
    summary += " | health=" + backendHealth_.status;
    if (!backendModels_.stems.empty())
    {
        summary += " | stems=" + backendModels_.stems.front();
    }
    if (!backendModels_.rewrite.empty())
    {
        summary += " | rewrite=" + backendModels_.rewrite.front();
    }
    if (!backendModels_.addLayer.empty())
    {
        summary += " | add-layer=" + backendModels_.addLayer.front();
    }
    if (transport_->canUseProjectPlayback())
    {
        summary += " | preview=live";
    }
    else
    {
        summary += previewPlaybackDirty_ ? " | preview=stale" : " | preview=fresh";
        summary += " | live-disabled=" + transport_->projectPlaybackDiagnostic();
    }
    summary += " | timeline=" + timeline_->backendSummary();
    summary += " | transport=" + transport_->backendSummary();
    summary += " | sync=" + projectManager_->state().engineState.tracktionSyncState;
    summary += " | route=" + transport_->playbackRouteSummary();
    summary += " | url=" + aiJobClient_->backendUrl();
    return summary;
}

bool AppController::separateStemsForSelectedClip()
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr)
    {
        logger_->error("Separate stems requested with no selected clip");
        return false;
    }

    const auto assetPath = resolveAssetPath(state, *clip);
    if (assetPath.empty())
    {
        logger_->error("Selected clip has no asset path");
        return false;
    }

    taskManager_->queueStems(clip->id, assetPath, clip->startSec);
    markProjectDirty();
    return true;
}

bool AppController::rewriteSelectedRegion(const std::string& prompt)
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr || !state.uiState.hasSelectedRegion)
    {
        logger_->error("Rewrite requested without selected clip/region");
        return false;
    }

    const auto duration = std::max(0.1, state.uiState.selectedRegionEndSec - state.uiState.selectedRegionStartSec);
    const auto tempPath = projectManager_->cacheDirectory() / (clip->id + "_rewrite_region.wav");
    if (!exportService_->exportRegion(state, state.uiState.selectedRegionStartSec, state.uiState.selectedRegionEndSec, tempPath))
    {
        logger_->error("Failed to export temp region for rewrite");
        return false;
    }

    taskManager_->queueRewrite(clip->id, tempPath.string(), state.uiState.selectedRegionStartSec, duration, prompt);
    markProjectDirty();
    return true;
}

bool AppController::addGeneratedLayer(const std::string& prompt)
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr || !state.uiState.hasSelectedRegion)
    {
        logger_->error("Add layer requested without selected clip/region");
        return false;
    }

    const auto duration = std::max(0.1, state.uiState.selectedRegionEndSec - state.uiState.selectedRegionStartSec);
    const auto tempPath = projectManager_->cacheDirectory() / (clip->id + "_add_layer_region.wav");
    if (!exportService_->exportRegion(state, state.uiState.selectedRegionStartSec, state.uiState.selectedRegionEndSec, tempPath))
    {
        logger_->error("Failed to export temp region for add-layer");
        return false;
    }

    taskManager_->queueAddLayer(clip->id, tempPath.string(), state.uiState.selectedRegionStartSec, duration, prompt);
    markProjectDirty();
    return true;
}

bool AppController::exportFullMix(const std::string& outputPath)
{
    return exportService_->exportMix(projectManager_->state(), outputPath);
}

bool AppController::exportSelectedRegion(const std::string& outputPath)
{
    const auto& state = projectManager_->state();
    if (!state.uiState.hasSelectedRegion)
    {
        logger_->error("Export selected region requested with no selected region");
        return false;
    }

    return exportService_->exportRegion(
        state,
        state.uiState.selectedRegionStartSec,
        state.uiState.selectedRegionEndSec,
        outputPath);
}

bool AppController::exportStemTracks(const std::string& outputDirectory)
{
    return exportService_->exportStemTracks(projectManager_->state(), outputDirectory);
}

bool AppController::duplicateSelectedClip()
{
    return finalizeTimelineEdit(
        [this]()
        {
            return clipOperations_->duplicateSelected(projectManager_->state());
        },
        false,
        {},
        "duplicateSelectedClip");
}

bool AppController::deleteSelectedClip()
{
    return finalizeTimelineEdit(
        [this]()
        {
            return clipOperations_->deleteSelected(projectManager_->state());
        },
        true,
        {},
        "deleteSelectedClip");
}

bool AppController::splitSelectedClipAtPlayhead()
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr)
    {
        logger_->warning("Split clip skipped because no clip is selected");
        return false;
    }

    const auto timelineSplitSec = currentTimelinePlayheadSec();
    return finalizeTimelineEdit(
        [this, &state, timelineSplitSec]()
        {
            return clipOperations_->splitSelected(state, timelineSplitSec);
        },
        true,
        "Split clip skipped because playhead is outside the selected clip body",
        "splitSelectedClip");
}

bool AppController::setSelectedClipGain(double gain)
{
    return finalizeTimelineEdit(
        [this, gain]()
        {
            return clipOperations_->setSelectedGain(projectManager_->state(), gain);
        },
        false,
        {},
        "setSelectedClipGain");
}

bool AppController::setSelectedClipFadeIn(double fadeSec)
{
    return finalizeTimelineEdit(
        [this, fadeSec]()
        {
            return clipOperations_->setSelectedFadeIn(projectManager_->state(), fadeSec);
        },
        false,
        {},
        "setSelectedClipFadeIn");
}

bool AppController::setSelectedClipFadeOut(double fadeSec)
{
    return finalizeTimelineEdit(
        [this, fadeSec]()
        {
            return clipOperations_->setSelectedFadeOut(projectManager_->state(), fadeSec);
        },
        false,
        {},
        "setSelectedClipFadeOut");
}

bool AppController::activateSelectedTake()
{
    return finalizeTimelineEdit(
        [this]()
        {
            return clipOperations_->activateSelectedTake(projectManager_->state());
        },
        false,
        "Activate take skipped because selected clip is not part of a take group",
        "activateSelectedTake");
}

bool AppController::trimSelectedClipLeft(double deltaSec)
{
    return finalizeTimelineEdit(
        [this, deltaSec]()
        {
            return clipOperations_->trimSelectedLeft(projectManager_->state(), deltaSec);
        },
        true,
        {},
        "trimSelectedClipLeft");
}

bool AppController::trimSelectedClipRight(double deltaSec)
{
    return finalizeTimelineEdit(
        [this, deltaSec]()
        {
            return clipOperations_->trimSelectedRight(projectManager_->state(), deltaSec);
        },
        true,
        {},
        "trimSelectedClipRight");
}

bool AppController::createCrossfadeWithPrevious(double overlapSec)
{
    return finalizeTimelineEdit(
        [this, overlapSec]()
        {
            return clipOperations_->createCrossfadeWithPrevious(projectManager_->state(), overlapSec);
        },
        true,
        "Create previous crossfade skipped because no suitable previous clip was found",
        "createCrossfadeWithPrevious");
}

bool AppController::createCrossfadeWithNext(double overlapSec)
{
    return finalizeTimelineEdit(
        [this, overlapSec]()
        {
            return clipOperations_->createCrossfadeWithNext(projectManager_->state(), overlapSec);
        },
        true,
        "Create next crossfade skipped because no suitable next clip was found",
        "createCrossfadeWithNext");
}

bool AppController::moveClipOnTimeline(const std::string& clipId, double newStartSec)
{
    return finalizeTimelineEdit(
        [this, &clipId, newStartSec]()
        {
            return timeline_->moveClip(projectManager_->state(), clipId, newStartSec);
        },
        true,
        {},
        "moveClipOnTimeline");
}

void AppController::beginInteractiveTimelineEdit()
{
    if (interactiveTimelineEditActive_)
    {
        return;
    }

    interactiveTimelineEditActive_ = true;
    interactiveTimelineEditWasPlaying_ = transport_->isPlaying();
    interactiveTimelineEditPlayheadSec_ = currentTimelinePlayheadSec();
    if (interactiveTimelineEditWasPlaying_)
    {
        transport_->pause();
        previewPlaybackActive_ = false;
        logger_->info("Paused transport for interactive timeline edit");
    }
}

void AppController::finishInteractiveTimelineEdit(bool changed, bool syncTransportToSelectionAfterEdit)
{
    if (!interactiveTimelineEditActive_)
    {
        return;
    }

    interactiveTimelineEditActive_ = false;
    if (changed)
    {
        markPreviewPlaybackDirty();
        markProjectDirty();
        if (syncTransportToSelectionAfterEdit)
        {
            syncTransportToSelection();
        }
        saveProject();
    }

    restorePlaybackAfterTimelineEdit(interactiveTimelineEditWasPlaying_, interactiveTimelineEditPlayheadSec_);
    interactiveTimelineEditWasPlaying_ = false;
    interactiveTimelineEditPlayheadSec_ = 0.0;
}

bool AppController::toggleTrackMute(const std::string& trackId)
{
    return finalizeTimelineEdit(
        [this, &trackId]()
        {
            return timeline_->toggleTrackMute(projectManager_->state(), trackId);
        },
        false,
        {},
        "toggleTrackMute");
}

bool AppController::toggleTrackSolo(const std::string& trackId)
{
    return finalizeTimelineEdit(
        [this, &trackId]()
        {
            return timeline_->toggleTrackSolo(projectManager_->state(), trackId);
        },
        false,
        {},
        "toggleTrackSolo");
}

void AppController::playTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "play");
    if (transport_->canUseProjectPlayback() && shouldUseProjectPreview())
    {
        transport_->useProjectPlayback(true);
        auto& state = projectManager_->state();
        transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec() > 0.0 ? transport_->sourceDurationSec() : state.uiState.playheadSec));
        transport_->play();
        previewPlaybackActive_ = true;
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState("live project playback active");
        logger_->info("Started realtime project playback");
        return;
    }

    if (shouldUseProjectPreview() && preparePreviewPlayback())
    {
        auto& state = projectManager_->state();
        transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
        transport_->play();
        previewPlaybackActive_ = true;
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState("cached preview playback active");
        logger_->info("Started project preview playback");
        return;
    }

    previewPlaybackActive_ = false;
    syncTransportToSelection();
    if (!transport_->hasLoadedSource() && !transport_->usingProjectPlayback())
    {
        logger_->warning("Transport play skipped because no valid selected source is available");
        syncEngineIntegrationState("transport play skipped; no valid source is available");
        return;
    }
    transport_->play();
    syncEngineIntegrationState("selected-source playback active");
}

void AppController::pauseTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "pause");
    transport_->pause();
    if (previewPlaybackActive_)
    {
        projectManager_->state().uiState.playheadSec = transport_->playheadSec();
    }
    syncEngineIntegrationState("transport paused");
}

void AppController::stopTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "stop");
    transport_->stop();
    auto& state = projectManager_->state();
    previewPlaybackActive_ = false;
    syncTransportToSelection();
    state.uiState.playheadSec = 0.0;
    syncEngineIntegrationState("transport stopped");
}

moon::engine::Settings AppController::currentSettings() const
{
    return settings_;
}

bool AppController::saveSettings(const moon::engine::Settings& settings)
{
    settings_ = settings;
    const auto saved = settingsService_->save(settings_);
    if (saved)
    {
        logger_->info("Saved app settings to " + settingsService_->settingsPath().string());
        logger_->info("Restart the app to fully apply backend URL changes.");
    }
    else
    {
        logger_->error("Failed to save app settings");
    }
    return saved;
}

void AppController::pollTasks()
{
    auto& state = projectManager_->state();
    const auto clipCountBefore = state.clips.size();
    const auto generatedCountBefore = state.generatedAssets.size();

    taskManager_->poll(state, *timeline_);

    if (state.clips.size() != clipCountBefore || state.generatedAssets.size() != generatedCountBefore)
    {
        markPreviewPlaybackDirty();
        markProjectDirty();
        saveProject();
    }
}

void AppController::autosaveIfNeeded()
{
    projectManager_->autosaveProject();
}

void AppController::syncTransportToSelection()
{
    auto& state = projectManager_->state();
    if (previewPlaybackActive_)
    {
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState("preview playback state synced");
        return;
    }

    if (shouldUseProjectPreview())
    {
        if (transport_->canUseProjectPlayback())
        {
            runtimeCoordinator_->noteTransportOperation(*projectManager_, "sync-live-project");
            transport_->useProjectPlayback(true);
            state.uiState.playheadSec = transport_->playheadSec();
            syncEngineIntegrationState("sync via live project playback");
            return;
        }
        if (preparePreviewPlayback())
        {
            runtimeCoordinator_->noteTransportOperation(*projectManager_, "sync-cached-preview");
            const auto timelineSec = std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec());
            if (std::abs(transport_->playheadSec() - timelineSec) > 0.05)
            {
                transport_->seek(timelineSec);
            }
            state.uiState.playheadSec = transport_->playheadSec();
            syncEngineIntegrationState("sync via cached preview playback");
            return;
        }
    }

    if (state.uiState.selectedClipId.empty())
    {
        previewPlaybackActive_ = false;
        transport_->clearLoadedSource();
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState("sync with no selected clip; transport cleared");
        return;
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id != state.uiState.selectedClipId)
        {
            continue;
        }

        const auto assetPath = resolveAssetPath(state, clip);
        if (!assetPath.empty())
        {
            runtimeCoordinator_->noteTransportOperation(*projectManager_, "sync-selected-source");
            if (transport_->sourcePath() != assetPath || std::abs(transport_->sourceDurationSec() - clip.durationSec) > 0.0001)
            {
                transport_->loadSource(assetPath, clip.durationSec);
            }
            if (transport_->hasLoadedSource())
            {
                state.uiState.playheadSec = clip.startSec + transport_->playheadSec();
                syncEngineIntegrationState("sync via selected clip source");
            }
            else
            {
                logger_->warning("Selected clip source could not be loaded for transport sync: " + assetPath);
                syncEngineIntegrationState("selected clip source load failed");
            }
        }
        else
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
            logger_->warning("Selected clip has no resolvable asset path; transport cleared");
            syncEngineIntegrationState("selected clip asset missing; transport cleared");
        }
        return;
    }

    clearSelectedClipState(state);
    previewPlaybackActive_ = false;
    transport_->clearLoadedSource();
    syncEngineIntegrationState("selected clip no longer exists; transport cleared");
}

void AppController::seekTimelinePlayhead(double timelineSec)
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "seek");
    auto& state = projectManager_->state();
    const auto clampedTimelineSec = std::max(0.0, timelineSec);
    state.uiState.playheadSec = clampedTimelineSec;

    if (previewPlaybackActive_ || shouldUseProjectPreview())
    {
        if (transport_->canUseProjectPlayback() && shouldUseProjectPreview())
        {
            transport_->useProjectPlayback(true);
        }
        else if (shouldUseProjectPreview() && !previewPlaybackActive_)
        {
            preparePreviewPlayback();
        }

        if (transport_->sourceDurationSec() > 0.0)
        {
            transport_->seek(std::clamp(clampedTimelineSec, 0.0, transport_->sourceDurationSec()));
            state.uiState.playheadSec = transport_->playheadSec();
            syncEngineIntegrationState("timeline seek via project playback");
            return;
        }
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id != state.uiState.selectedClipId)
        {
            continue;
        }

        const auto clipLocalSec = std::clamp(clampedTimelineSec - clip.startSec, 0.0, clip.durationSec);
        transport_->seek(clipLocalSec);
        state.uiState.playheadSec = clip.startSec + transport_->playheadSec();
        syncEngineIntegrationState("timeline seek via selected clip");
        return;
    }

    transport_->seek(clampedTimelineSec);
    syncEngineIntegrationState("timeline seek direct");
}

void AppController::nudgeTimelinePlayhead(double deltaSec)
{
    seekTimelinePlayhead(projectManager_->state().uiState.playheadSec + deltaSec);
}

void AppController::clearSelectedRegion()
{
    timeline_->clearSelectedRegion(projectManager_->state());
}

bool AppController::refreshBackendStatus()
{
    backendHealth_ = aiJobClient_->healthCheck();
    backendModels_ = aiJobClient_->models();

    if (aiJobClient_->backendReachable())
    {
        logger_->info("Backend refresh succeeded: " + backendHealth_.status);
        backendFallbackNoticeActive_ = false;
        refreshStartupNoticeState();
        return true;
    }

    logger_->warning("Backend refresh failed; stub fallback remains active");
    backendFallbackNoticeActive_ = true;
    refreshStartupNoticeState();
    return false;
}

bool AppController::rebuildPreviewPlayback()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "rebuild-preview");
    if (!shouldUseProjectPreview())
    {
        logger_->warning("Preview rebuild skipped because the project has no clips");
        return false;
    }

    const auto wasPlaying = transport_->isPlaying();
    const auto currentPlayhead = projectManager_->state().uiState.playheadSec;
    if (wasPlaying)
    {
        transport_->pause();
    }

    markPreviewPlaybackDirty();
    if (!preparePreviewPlayback())
    {
        logger_->error("Preview rebuild failed");
        return false;
    }

    transport_->seek(std::clamp(currentPlayhead, 0.0, transport_->sourceDurationSec()));
    projectManager_->state().uiState.playheadSec = transport_->playheadSec();

    if (wasPlaying)
    {
        transport_->play();
        previewPlaybackActive_ = true;
    }

    syncEngineIntegrationState("preview cache rebuilt");
    logger_->info("Rebuilt project preview playback cache");
    return true;
}

void AppController::maintainPreviewPlayback()
{
    if (!shouldUseProjectPreview() || transport_->isPlaying())
    {
        return;
    }

    if (transport_->canUseProjectPlayback())
    {
        runtimeCoordinator_->noteTransportOperation(*projectManager_, "idle-live-project");
        transport_->useProjectPlayback(true);
        syncEngineIntegrationState("idle live project playback ready");
        return;
    }

    if (!previewPlaybackDirty_
        && transport_->sourcePath() == previewMixPath_.string()
        && transport_->sourceDurationSec() > 0.0)
    {
        return;
    }

    const auto currentPlayhead = projectManager_->state().uiState.playheadSec;
    if (!preparePreviewPlayback())
    {
        return;
    }

    transport_->seek(std::clamp(currentPlayhead, 0.0, transport_->sourceDurationSec()));
    projectManager_->state().uiState.playheadSec = transport_->playheadSec();
    syncEngineIntegrationState("idle cached preview refreshed");
}

std::string AppController::windowTitle() const
{
    const auto& state = projectManager_->state();
    std::string title = "Moon Audio Editor";
    if (!state.projectName.empty())
    {
        title += " - " + state.projectName;
    }
    if (projectDirty_)
    {
        title += " *";
    }
    return title;
}

void AppController::markPreviewPlaybackDirty()
{
    previewPlaybackDirty_ = true;
}

double AppController::currentTimelinePlayheadSec() const
{
    const auto& state = projectManager_->state();
    if (previewPlaybackActive_ || shouldUseProjectPreview())
    {
        return state.uiState.playheadSec;
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id == state.uiState.selectedClipId)
        {
            return clip.startSec + transport_->playheadSec();
        }
    }

    return transport_->playheadSec();
}

bool AppController::finalizeTimelineEdit(const std::function<bool()>& editOperation,
                                         bool syncTransportToSelectionAfterEdit,
                                         const std::string& failureWarning,
                                         const std::string& operationName)
{
    const auto wasPlaying = transport_->isPlaying();
    const auto timelinePlayheadSec = currentTimelinePlayheadSec();
    if (wasPlaying)
    {
        transport_->pause();
        previewPlaybackActive_ = false;
    }

    const auto updated = editOperation();
    if (!updated)
    {
        if (!failureWarning.empty())
        {
            logger_->warning(failureWarning);
        }
        if (wasPlaying)
        {
            restorePlaybackAfterTimelineEdit(true, timelinePlayheadSec);
        }
        return false;
    }

    runtimeCoordinator_->noteTimelineOperation(*projectManager_, operationName);
    markPreviewPlaybackDirty();
    markProjectDirty();
    if (syncTransportToSelectionAfterEdit)
    {
        syncTransportToSelection();
    }
    saveProject();
    restorePlaybackAfterTimelineEdit(wasPlaying, timelinePlayheadSec);
    return true;
}

void AppController::restorePlaybackAfterTimelineEdit(bool wasPlaying, double timelinePlayheadSec)
{
    auto& state = projectManager_->state();
    state.uiState.playheadSec = std::max(0.0, timelinePlayheadSec);

    if (shouldUseProjectPreview())
    {
        if (transport_->canUseProjectPlayback())
        {
            transport_->useProjectPlayback(true);
            transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
            state.uiState.playheadSec = transport_->playheadSec();
            if (wasPlaying)
            {
                transport_->play();
                previewPlaybackActive_ = true;
            }
            return;
        }

        if (preparePreviewPlayback())
        {
            transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
            state.uiState.playheadSec = transport_->playheadSec();
            if (wasPlaying)
            {
                transport_->play();
                previewPlaybackActive_ = true;
            }
            return;
        }
    }

    syncTransportToSelection();
    if (!state.uiState.selectedClipId.empty())
    {
        seekTimelinePlayhead(state.uiState.playheadSec);
    }

    const bool hasPlayableSource = transport_->usingProjectPlayback()
        || (!transport_->sourcePath().empty() && transport_->sourceDurationSec() > 0.0);
    if (wasPlaying && hasPlayableSource)
    {
        transport_->play();
    }
    else if (wasPlaying && !hasPlayableSource)
    {
        logger_->warning("Playback was not resumed after edit because no valid source remained loaded");
    }
}

bool AppController::shouldUseProjectPreview() const
{
    const auto& state = projectManager_->state();
    return !state.clips.empty();
}

bool AppController::preparePreviewPlayback()
{
    auto& state = projectManager_->state();
    const auto previewRouteWasActive = transport_->usingProjectPlayback()
        || (!previewMixPath_.empty() && transport_->sourcePath() == previewMixPath_.string());

    if (state.clips.empty())
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        return false;
    }

    const auto durationSec = std::max(0.1, exportService_->estimateMixDuration(state));
    if (durationSec <= 0.0)
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        return false;
    }

    if (projectManager_->cacheDirectory().empty())
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        return false;
    }

    previewMixPath_ = projectManager_->cacheDirectory() / "timeline_preview_mix.wav";
    if (previewPlaybackDirty_ || transport_->sourcePath() != previewMixPath_.string() || !std::filesystem::exists(previewMixPath_))
    {
        if (!exportService_->exportMix(state, previewMixPath_))
        {
            if (previewRouteWasActive)
            {
                previewPlaybackActive_ = false;
                transport_->clearLoadedSource();
            }
            logger_->warning("Falling back to selected clip playback because preview mix render failed");
            return false;
        }
        previewPlaybackDirty_ = false;
        logger_->info("Prepared cached preview playback because realtime project playback is unavailable: "
                      + transport_->projectPlaybackDiagnostic());
    }

    if (transport_->sourcePath() != previewMixPath_.string()
        || std::abs(transport_->sourceDurationSec() - durationSec) > 0.0001)
    {
        transport_->loadSource(previewMixPath_.string(), durationSec);
    }

    if (!transport_->hasLoadedSource())
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        logger_->warning("Preview playback path could not be loaded after rendering");
        return false;
    }

    return true;
}

bool AppController::canCloseSafely() const
{
    return taskManager_->activeTaskCount() == 0;
}

void AppController::prepareForShutdown()
{
    autosaveIfNeeded();
    saveProject();
    logger_->info("Prepared application state for shutdown");
}

std::string AppController::startupNotice() const
{
    std::string notice;
    if (backendFallbackNoticeActive_)
    {
        notice += "Local backend is not reachable. The app will use stub fallback jobs until the FastAPI service is started.";
    }
    if (autosaveRecoveryNoticeActive_)
    {
        if (!notice.empty())
        {
            notice += "\n\n";
        }
        notice += "Recovered project state from autosave.project.json in the default workspace project.";
    }
    return notice;
}

void AppController::clearStartupNotice()
{
    backendFallbackNoticeActive_ = false;
    autosaveRecoveryNoticeActive_ = false;
}

void AppController::markProjectDirty()
{
    projectDirty_ = true;
}

void AppController::clearProjectDirty()
{
    projectDirty_ = false;
}

void AppController::refreshStartupNoticeState()
{
}

void AppController::syncEngineIntegrationState(const std::string& reason)
{
    runtimeCoordinator_->sync(*projectManager_, *timeline_, *transport_, reason);
    logPlaybackRouteTransition(reason);
}

void AppController::logPlaybackRouteTransition(const std::string& reason)
{
    const auto currentRoute = transport_->playbackRouteSummary();
    const auto currentDiagnostic = transport_->projectPlaybackDiagnostic();
    if (currentRoute == lastPlaybackRouteSummary_ && currentDiagnostic == lastPlaybackDiagnostic_)
    {
        return;
    }

    std::string message = "Playback route transition: ";
    if (lastPlaybackRouteSummary_.empty())
    {
        message += "<unset>";
    }
    else
    {
        message += lastPlaybackRouteSummary_;
    }
    message += " -> " + currentRoute;

    if (!reason.empty())
    {
        message += " | reason=" + reason;
    }

    if (currentRoute == "project-live")
    {
        logger_->info(message + " | live mix source active");
    }
    else if (currentRoute == "project-cached-preview")
    {
        logger_->warning(message + " | cached preview fallback active | diagnostic=" + currentDiagnostic);
    }
    else if (currentRoute == "selected-source")
    {
        logger_->info(message + " | selected clip source active");
    }
    else
    {
        logger_->warning(message + " | no valid playback source loaded | diagnostic=" + currentDiagnostic);
    }

    lastPlaybackRouteSummary_ = currentRoute;
    lastPlaybackDiagnostic_ = currentDiagnostic;
}
}
