#include "AppController.h"

#if MOON_HAS_JUCE
#include <juce_audio_formats/juce_audio_formats.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "AppConfig.h"

namespace moon::app
{
namespace
{
void appendBootstrapTrace(const std::string& line)
{
#if defined(_WIN32)
    if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
    {
        const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::app);
        if (out)
        {
            out << line << "\n";
        }
        return;
    }
#endif
}

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

#if MOON_HAS_JUCE
struct AudioImportMetadata
{
    double durationSec{0.0};
    int sampleRate{0};
};

AudioImportMetadata readAudioImportMetadata(const std::string& audioPath)
{
    AudioImportMetadata metadata;
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(juce::File(audioPath)));
    if (reader == nullptr)
    {
        return metadata;
    }

    metadata.sampleRate = static_cast<int>(reader->sampleRate);
    if (reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
    {
        metadata.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    }
    return metadata;
}
#endif

void clearSelectedClipState(moon::engine::ProjectState& state)
{
    state.uiState.selectedClipId.clear();
    for (auto& clip : state.clips)
    {
        clip.selected = false;
    }
}

std::string trimCopy(std::string value)
{
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
    return value;
}
}

AppController::AppController()
{
    appendBootstrapTrace("[bootstrap] AppController ctor begin");
    logger_ = std::make_unique<moon::engine::Logger>();
    appendBootstrapTrace("[bootstrap] logger created");
    settingsService_ = std::make_unique<moon::engine::SettingsService>();
    appendBootstrapTrace("[bootstrap] settings service created");
    settings_ = settingsService_->load();
    appendBootstrapTrace("[bootstrap] settings loaded");
    projectManager_ = std::make_unique<moon::engine::ProjectManager>(*logger_);
    appendBootstrapTrace("[bootstrap] project manager created");
    runtimeCoordinator_ = std::make_unique<moon::engine::EngineRuntimeCoordinator>(*logger_);
    appendBootstrapTrace("[bootstrap] runtime coordinator created");
    timeline_ = std::make_unique<moon::engine::TimelineFacade>(*logger_);
    appendBootstrapTrace("[bootstrap] timeline facade created");
    clipOperations_ = std::make_unique<moon::engine::ClipOperations>(*logger_);
    appendBootstrapTrace("[bootstrap] clip operations created");
    transport_ = std::make_unique<moon::engine::TransportFacade>(*logger_);
    appendBootstrapTrace("[bootstrap] transport facade created");
    waveformService_ = std::make_unique<moon::engine::WaveformService>(*logger_);
    appendBootstrapTrace("[bootstrap] waveform service created");
    exportService_ = std::make_unique<moon::engine::ExportService>(*logger_);
    appendBootstrapTrace("[bootstrap] export service created");
    aiJobClient_ = std::make_unique<moon::engine::LocalJobClient>(
        settings_.backendUrl.empty() ? std::string(AppConfig::backendUrl) : settings_.backendUrl,
        *logger_);
    appendBootstrapTrace("[bootstrap] ai job client created");
    taskManager_ = std::make_unique<moon::engine::TaskManager>(*aiJobClient_, *logger_);
    appendBootstrapTrace("[bootstrap] task manager created");

    transport_->setProjectState(&projectManager_->state());
#if MOON_HAS_TRACKTION
    timeline_->setPreferredBackend(moon::engine::TimelineBackendMode::TracktionHybrid);
    transport_->setPreferredBackend(moon::engine::TransportBackendMode::TracktionHybrid);
#endif
    syncEngineIntegrationState();
    appendBootstrapTrace("[bootstrap] AppController ctor end");
}

bool AppController::startup()
{
    logger_->info("AppController startup");

    // Fast-start path: do not block the UI on backend probing.
    backendFallbackNoticeActive_ = false;
    autosaveRecoveryNoticeActive_ = false;
    refreshStartupNoticeState();

    if (!projectManager_->projectFilePath().has_value())
    {
        const auto defaultRoot = (std::filesystem::current_path() / "workspace_project").string();
        createProject("Untitled Project", defaultRoot);
        logger_->info("Created default startup project at " + defaultRoot);
    }

    refreshBackendStatus();
    logger_->info("Startup completed without blocking backend probe");
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
    auto& state = projectManager_->state();
    if (state.tracks.empty())
    {
        timeline_->ensureTrack(state, "Track 1");
    }

    return importAudioToTrack(audioPath, state.tracks.front().id, 0.0);
}

bool AppController::importAudioToTrack(const std::string& audioPath, const std::string& trackId, double startSec)
{
    if (!std::filesystem::exists(audioPath))
    {
        logger_->error("Import failed, file not found: " + audioPath);
        return false;
    }

    auto& state = projectManager_->state();
    const bool wasEmptyProject = state.clips.empty();
    waveformService_->requestWaveform(audioPath);
    const auto waveformSnapshot = waveformService_->snapshotFor(audioPath);

    int detectedSampleRate = 0;
    double detectedDurationSec = 0.0;
#if MOON_HAS_JUCE
    const auto importMetadata = readAudioImportMetadata(audioPath);
    detectedSampleRate = importMetadata.sampleRate;
    detectedDurationSec = importMetadata.durationSec;
#endif

    if (detectedSampleRate <= 0 && waveformSnapshot.data != nullptr && waveformSnapshot.data->sampleRate > 0)
    {
        detectedSampleRate = waveformSnapshot.data->sampleRate;
    }

    if (detectedDurationSec <= 0.0 && waveformSnapshot.data != nullptr)
    {
        detectedDurationSec = waveformSnapshot.data->durationSec;
    }

    if (wasEmptyProject && detectedSampleRate > 0)
    {
        state.sampleRate = detectedSampleRate;
        logger_->info("Adjusted project sample rate to imported WAV sample rate: " + std::to_string(state.sampleRate));
    }

    const auto durationSec = std::max(0.1, detectedDurationSec);
    const auto resolvedTrackId = !trackId.empty() ? trackId : timeline_->ensureTrack(state, "Track 1");
    const auto clipId = timeline_->insertAudioClip(state, resolvedTrackId, audioPath, std::max(0.0, startSec), durationSec);
    timeline_->selectClip(state, clipId);
    state.uiState.selectedTrackId = resolvedTrackId;
    state.uiState.playheadSec = std::max(0.0, startSec);
    markPreviewPlaybackDirty();
    markProjectDirty();
    logger_->info("Imported audio clip into project playback path: " + clipId);
    return true;
}

std::optional<std::string> AppController::projectFilePath() const
{
    return projectManager_->projectFilePath();
}

std::string AppController::backendStatusSummary() const
{
    std::string summary = backendHealth_.backend == "local-job-service" ? "Local jobs" : (aiJobClient_->backendReachable() ? "Backend live" : "Backend fallback");
    const auto route = transport_->playbackRouteSummary();
    if (route == "project-live")
    {
        summary += " | live";
    }
    else if (route == "project-cached-preview")
    {
        summary += previewPlaybackDirty_ ? " | stale preview" : " | cached preview";
    }
    else
    {
        summary += previewPlaybackDirty_ ? " | stale" : " | " + route;
    }

    const auto diagnostic = transport_->projectPlaybackDiagnostic();
    if (!diagnostic.empty() && diagnostic != "ok")
    {
        summary += " | " + diagnostic;
    }

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

std::optional<std::string> AppController::generateMusic(const moon::engine::MusicGenerationRequest& request)
{
    const auto stylesPrompt = trimCopy(request.stylesPrompt);
    const auto lyricsPrompt = trimCopy(request.lyricsPrompt);
    if (stylesPrompt.empty() && lyricsPrompt.empty())
    {
        logger_->error("Music generation requested with empty styles and lyrics");
        return std::nullopt;
    }

    const auto models = availableMusicGenerationModels();
    if (models.empty())
    {
        logger_->error("Acestep music generation is unavailable");
        return std::nullopt;
    }

    auto queuedRequest = request;
    queuedRequest.stylesPrompt = stylesPrompt;
    queuedRequest.lyricsPrompt = lyricsPrompt;
    queuedRequest.isInstrumental = lyricsPrompt.empty();
    if (queuedRequest.selectedModel.empty())
    {
        queuedRequest.selectedModel = models.front();
    }

    auto& state = projectManager_->state();
    const auto startSec = std::max(0.0, state.uiState.playheadSec);
    std::string targetTrackId;
    if (!state.uiState.selectedTrackId.empty())
    {
        const bool selectedTrackHasClip = std::any_of(
            state.clips.begin(),
            state.clips.end(),
            [&state](const moon::engine::ClipInfo& clip)
            {
                return clip.trackId == state.uiState.selectedTrackId;
            });
        if (!selectedTrackHasClip)
        {
            targetTrackId = state.uiState.selectedTrackId;
        }
    }

    const auto preferredTrackName = "Generated " + std::string(moon::engine::musicGenerationCategoryLabel(queuedRequest.category));
    const auto jobId = taskManager_->queueMusicGeneration(queuedRequest, targetTrackId, preferredTrackName, startSec);
    markProjectDirty();
    return jobId;
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

bool AppController::moveClipToTrack(const std::string& clipId, const std::string& trackId, double newStartSec)
{
    return finalizeTimelineEdit(
        [this, &clipId, &trackId, newStartSec]()
        {
            return timeline_->moveClipToTrack(projectManager_->state(), clipId, trackId, newStartSec);
        },
        true,
        {},
        "moveClipToTrack");
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
    if (interactiveTimelineEditWasPlaying_ && !shouldUseProjectPreview())
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
        if (syncTransportToSelectionAfterEdit && !interactiveTimelineEditWasPlaying_)
        {
            syncTransportToSelection();
        }
        saveProject();
    }

    if (interactiveTimelineEditWasPlaying_ || changed)
    {
        restorePlaybackAfterTimelineEdit(interactiveTimelineEditWasPlaying_, interactiveTimelineEditPlayheadSec_);
    }
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

bool AppController::renameTrack(const std::string& trackId, const std::string& newName)
{
    auto sanitizedName = newName;
    sanitizedName.erase(sanitizedName.begin(), std::find_if(sanitizedName.begin(), sanitizedName.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    sanitizedName.erase(std::find_if(sanitizedName.rbegin(), sanitizedName.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), sanitizedName.end());
    if (sanitizedName.empty())
    {
        for (std::size_t index = 0; index < projectManager_->state().tracks.size(); ++index)
        {
            if (projectManager_->state().tracks[index].id == trackId)
            {
                sanitizedName = "Track " + std::to_string(index + 1);
                break;
            }
        }
    }

    return finalizeTimelineEdit(
        [this, &trackId, &sanitizedName]()
        {
            return timeline_->renameTrack(projectManager_->state(), trackId, sanitizedName);
        },
        false,
        {},
        "renameTrack");
}

bool AppController::deleteTrack(const std::string& trackId)
{
    return finalizeTimelineEdit(
        [this, &trackId]()
        {
            return timeline_->deleteTrack(projectManager_->state(), trackId);
        },
        true,
        {},
        "deleteTrack");
}

bool AppController::setTrackColor(const std::string& trackId, const std::string& colorHex)
{
    return finalizeTimelineEdit(
        [this, &trackId, &colorHex]()
        {
            return timeline_->setTrackColor(projectManager_->state(), trackId, colorHex);
        },
        false,
        {},
        "setTrackColor");
}

void AppController::playTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "play");
    if (shouldUseProjectPreview() && ensureProjectPlaybackRoute())
    {
        auto& state = projectManager_->state();
        transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
        transport_->play();
        previewPlaybackActive_ = !transport_->usingProjectPlayback();
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState(transport_->usingProjectPlayback() ? "live project playback active" : "cached project playback active");
        logger_->info(transport_->usingProjectPlayback() ? "Started live project playback" : "Started cached project playback");
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
    projectManager_->state().uiState.playheadSec = transport_->playheadSec();
    syncEngineIntegrationState("transport paused");
}

void AppController::stopTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "stop");
    transport_->stop();
    auto& state = projectManager_->state();
    previewPlaybackActive_ = false;
    state.uiState.playheadSec = 0.0;
    if (shouldUseProjectPreview() && ensureProjectPlaybackRoute())
    {
        transport_->seek(0.0);
        syncEngineIntegrationState("transport stopped on project playback");
        return;
    }

    syncTransportToSelection();
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
    const auto selectedClipBefore = state.uiState.selectedClipId;

    taskManager_->poll(state, *timeline_);

    if (state.clips.size() != clipCountBefore || state.generatedAssets.size() != generatedCountBefore)
    {
        markPreviewPlaybackDirty();
        markProjectDirty();
        saveProject();
        if (state.uiState.selectedClipId != selectedClipBefore)
        {
            syncTransportToSelection();
        }
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
        if (ensureProjectPlaybackRoute())
        {
            runtimeCoordinator_->noteTransportOperation(
                *projectManager_,
                transport_->usingProjectPlayback() ? "sync-live-project" : "sync-cached-preview");
            const auto timelineSec = std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec());
            if (std::abs(transport_->playheadSec() - timelineSec) > 0.05)
            {
                transport_->seek(timelineSec);
            }
            state.uiState.playheadSec = transport_->playheadSec();
            syncEngineIntegrationState(transport_->usingProjectPlayback() ? "sync via live project playback" : "sync via cached project playback");
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
        if (shouldUseProjectPreview() && !previewPlaybackActive_)
        {
            ensureProjectPlaybackRoute();
        }

        if (transport_->sourceDurationSec() > 0.0 || transport_->usingProjectPlayback())
        {
            transport_->seek(std::clamp(clampedTimelineSec, 0.0, transport_->sourceDurationSec()));
            state.uiState.playheadSec = transport_->playheadSec();
            syncEngineIntegrationState(transport_->usingProjectPlayback() ? "timeline seek via live project playback" : "timeline seek via cached project playback");
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

    if (backendHealth_.backend == "local-job-service")
    {
        logger_->info("Local job services ready");
        backendFallbackNoticeActive_ = false;
        refreshStartupNoticeState();
        return true;
    }

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

void AppController::setBackendUrl(const std::string& backendUrl)
{
    settings_.backendUrl = backendUrl;
    if (aiJobClient_ != nullptr)
    {
        aiJobClient_->setBackendUrl(backendUrl);
    }
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

    if (shouldUseLiveProjectPlayback())
    {
        if (!transport_->usingProjectPlayback())
        {
            ensureProjectPlaybackRoute();
        }
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

void AppController::notifyProjectMixChanged()
{
    markPreviewPlaybackDirty();
    markProjectDirty();
    if (!transport_->isPlaying())
    {
        syncTransportToSelection();
    }
    syncEngineIntegrationState("project mix changed");
}

void AppController::refreshPlaybackUiState()
{
    auto& state = projectManager_->state();
    if (!transport_->isPlaying())
    {
        return;
    }

    if (transport_->usingProjectPlayback() || previewPlaybackActive_ || shouldUseProjectPreview())
    {
        state.uiState.playheadSec = transport_->playheadSec();
        return;
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id == state.uiState.selectedClipId)
        {
            state.uiState.playheadSec = clip.startSec + transport_->playheadSec();
            return;
        }
    }

    state.uiState.playheadSec = transport_->playheadSec();
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
    const bool usingProjectRoute = shouldUseProjectPreview();
    const auto wasPlaying = transport_->isPlaying();
    const auto timelinePlayheadSec = currentTimelinePlayheadSec();
    if (wasPlaying && !usingProjectRoute)
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
    if (syncTransportToSelectionAfterEdit && !wasPlaying)
    {
        syncTransportToSelection();
    }
    else if (!shouldUseProjectPreview())
    {
        syncTransportToSelection();
    }
    saveProject();
    if (wasPlaying || syncTransportToSelectionAfterEdit)
    {
        restorePlaybackAfterTimelineEdit(wasPlaying, timelinePlayheadSec);
    }
    return true;
}

void AppController::restorePlaybackAfterTimelineEdit(bool wasPlaying, double timelinePlayheadSec)
{
    auto& state = projectManager_->state();
    state.uiState.playheadSec = std::max(0.0, timelinePlayheadSec);

    if (shouldUseProjectPreview())
    {
        if (shouldUseLiveProjectPlayback())
        {
            if (ensureProjectPlaybackRoute())
            {
                if (!wasPlaying)
                {
                    transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
                    state.uiState.playheadSec = transport_->playheadSec();
                }
                else
                {
                    state.uiState.playheadSec = transport_->playheadSec();
                }

                if (wasPlaying && !transport_->isPlaying())
                {
                    transport_->play();
                }
                syncEngineIntegrationState("live project playback updated after edit");
                return;
            }
        }

        if (wasPlaying)
        {
            previewPlaybackActive_ = true;
            syncEngineIntegrationState("project playback marked stale after edit");
            return;
        }

        if (preparePreviewPlayback())
        {
            transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
            state.uiState.playheadSec = transport_->playheadSec();
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

bool AppController::shouldUseLiveProjectPlayback() const
{
    return shouldUseProjectPreview()
        && transport_->supportsProjectPlayback()
        && transport_->canUseProjectPlayback();
}

bool AppController::ensureProjectPlaybackRoute()
{
    if (!shouldUseProjectPreview())
    {
        return false;
    }

    if (shouldUseLiveProjectPlayback())
    {
        transport_->useProjectPlayback(true);
        previewPlaybackActive_ = false;
        previewPlaybackDirty_ = false;
        syncEngineIntegrationState("project playback routed live");
        return transport_->usingProjectPlayback() && transport_->sourceDurationSec() > 0.0;
    }

    if (transport_->usingProjectPlayback())
    {
        transport_->useProjectPlayback(false);
    }

    return preparePreviewPlayback();
}

bool AppController::preparePreviewPlayback()
{
    auto& state = projectManager_->state();
    const auto previewRouteWasActive = !previewMixPath_.empty()
        && transport_->sourcePath() == previewMixPath_.string();

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
    if (durationSec <= 0.0 || projectManager_->cacheDirectory().empty())
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        return false;
    }

    previewMixPath_ = projectManager_->cacheDirectory() / "timeline_preview_mix.wav";
    if (transport_->usingProjectPlayback())
    {
        transport_->useProjectPlayback(false);
    }

    if (previewPlaybackDirty_ || transport_->sourcePath() != previewMixPath_.string() || !std::filesystem::exists(previewMixPath_))
    {
        if (!exportService_->exportMix(state, previewMixPath_))
        {
            if (previewRouteWasActive)
            {
                previewPlaybackActive_ = false;
                transport_->clearLoadedSource();
            }
            logger_->warning("Project playback preview render failed");
            return false;
        }
        previewPlaybackDirty_ = false;
        logger_->info("Prepared project playback preview mix");
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
        logger_->warning("Project playback preview source could not be loaded");
        return false;
    }

    return true;
}

double AppController::projectPlaybackDurationSec() const
{
    if (shouldUseProjectPreview())
    {
        return std::max(0.1, exportService_->estimateMixDuration(projectManager_->state()));
    }

    if (transport_->sourceDurationSec() > 0.0)
    {
        return transport_->sourceDurationSec();
    }

    if (const auto* clip = findSelectedClip(projectManager_->state()))
    {
        return clip->durationSec;
    }

    return 0.1;
}

bool AppController::setProjectTempo(double tempo)
{
    const auto clampedTempo = std::clamp(tempo, 20.0, 300.0);
    auto& state = projectManager_->state();
    if (std::abs(state.tempo - clampedTempo) < 0.0001)
    {
        return false;
    }

    state.tempo = clampedTempo;
    markPreviewPlaybackDirty();
    syncTransportToSelection();
    syncEngineIntegrationState("project tempo updated");
    markProjectDirty();
    saveProject();
    logger_->info("Updated project tempo to " + std::to_string(clampedTempo) + " BPM");
    return true;
}

bool AppController::setProjectTimeSignature(int numerator, int denominator)
{
    const int sanitizedNumerator = std::clamp(numerator, 2, 12);
    const int sanitizedDenominator = denominator == 8 ? 8 : 4;
    auto& state = projectManager_->state();
    if (state.timeSignatureNumerator == sanitizedNumerator && state.timeSignatureDenominator == sanitizedDenominator)
    {
        return false;
    }

    state.timeSignatureNumerator = sanitizedNumerator;
    state.timeSignatureDenominator = sanitizedDenominator;
    markPreviewPlaybackDirty();
    syncTransportToSelection();
    syncEngineIntegrationState("project time signature updated");
    markProjectDirty();
    saveProject();
    logger_->info("Updated time signature to " + std::to_string(sanitizedNumerator) + "/" + std::to_string(sanitizedDenominator));
    return true;
}

std::vector<std::string> AppController::availableMusicGenerationModels() const
{
    return backendModels_.musicGeneration;
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
        notice += "Optional AI runners are not reachable. The app will use local stub jobs until real runners are configured.";
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
    if (aiJobClient_ != nullptr && aiJobClient_->backendReachable())
    {
        backendFallbackNoticeActive_ = false;
    }

    if (projectManager_ != nullptr && !projectManager_->hasAutosave())
    {
        autosaveRecoveryNoticeActive_ = false;
    }
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




