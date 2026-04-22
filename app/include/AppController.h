#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AIJobClient.h"
#include "BackendProcessManager.h"
#include "EngineRuntimeCoordinator.h"
#include "ExportService.h"
#include "LocalJobClient.h"
#include "Logger.h"
#include "ModelManager.h"
#include "MusicGeneration.h"
#include "ClipOperations.h"
#include "ProjectManager.h"
#include "SettingsService.h"
#include "TaskManager.h"
#include "TimelineFacade.h"
#include "TransportFacade.h"
#include "WaveformService.h"

namespace moon::app
{
struct RuntimeInstallDiagnostics
{
    bool active{false};
    bool hasLog{false};
    double progress{0.0};
    std::string source;
    std::string summary;
    std::string error;
    std::string logPath;
};

class AppController
{
public:
    AppController();
    ~AppController();

    bool startup();
    bool createProject(const std::string& name, const std::string& rootPath);
    bool openProject(const std::string& projectFilePath);
    bool restoreAutosave(const std::string& projectRootPath);
    bool saveProject();
    bool importAudio(const std::string& audioPath);
    bool importAudioToTrack(const std::string& audioPath, const std::string& trackId, double startSec);
    bool separateStemsForSelectedClip();
    bool rewriteSelectedRegion(const std::string& prompt);
    bool addGeneratedLayer(const std::string& prompt);
    std::optional<std::string> generateMusic(const moon::engine::MusicGenerationRequest& request);
    std::string lastMusicGenerationError() const { return lastMusicGenerationError_; }
    bool exportFullMix(const std::string& outputPath);
    bool exportSelectedRegion(const std::string& outputPath);
    bool exportStemTracks(const std::string& outputDirectory);
    bool duplicateSelectedClip();
    bool deleteSelectedClip();
    bool splitSelectedClipAtPlayhead();
    bool setSelectedClipGain(double gain);
    bool setSelectedClipFadeIn(double fadeSec);
    bool setSelectedClipFadeOut(double fadeSec);
    bool activateSelectedTake();
    bool trimSelectedClipLeft(double deltaSec);
    bool trimSelectedClipRight(double deltaSec);
    bool createCrossfadeWithPrevious(double overlapSec);
    bool createCrossfadeWithNext(double overlapSec);
    bool moveClipOnTimeline(const std::string& clipId, double newStartSec);
    bool moveClipToTrack(const std::string& clipId, const std::string& trackId, double newStartSec);
    void beginInteractiveTimelineEdit();
    void finishInteractiveTimelineEdit(bool changed, bool syncTransportToSelectionAfterEdit = true);
    bool toggleTrackMute(const std::string& trackId);
    bool toggleTrackSolo(const std::string& trackId);
    bool renameTrack(const std::string& trackId, const std::string& newName);
    bool deleteTrack(const std::string& trackId);
    bool setTrackColor(const std::string& trackId, const std::string& colorHex);
    void playTransport();
    void pauseTransport();
    void stopTransport();
    void autosaveIfNeeded();
    moon::engine::Settings currentSettings() const;
    bool saveSettings(const moon::engine::Settings& settings);
    bool pollTasks();
    void syncTransportToSelection();
    void seekTimelinePlayhead(double timelineSec);
    void nudgeTimelinePlayhead(double deltaSec);
    void clearSelectedRegion();
    bool canCloseSafely() const;
    void prepareForShutdown();
    bool refreshBackendStatus();
    void setBackendUrl(const std::string& backendUrl);
    bool rebuildPreviewPlayback();
    void maintainPreviewPlayback();
    void notifyProjectMixChanged();
    void refreshPlaybackUiState();
    std::string windowTitle() const;
    std::string startupNotice() const;
    void clearStartupNotice();
    double projectPlaybackDurationSec() const;
    double projectTempo() const noexcept { return projectManager_->state().tempo; }
    int projectTimeSignatureNumerator() const noexcept { return projectManager_->state().timeSignatureNumerator; }
    int projectTimeSignatureDenominator() const noexcept { return projectManager_->state().timeSignatureDenominator; }
    bool setProjectTempo(double tempo);
    bool setProjectTimeSignature(int numerator, int denominator);
    std::vector<std::string> availableMusicGenerationModels() const;
    const moon::engine::ModelRegistrySnapshot& modelRegistrySnapshot() const noexcept { return modelRegistrySnapshot_; }
    bool refreshModelRegistry(std::string* errorMessage = nullptr);
    bool syncRemoteModelCatalog(std::string* errorMessage = nullptr);
    bool pollModelOperations(std::string* errorMessage = nullptr);
    bool setActiveGenerationModel(moon::engine::ModelCapability capability, const std::string& modelId, std::string& errorMessage);
    bool addExistingModelFolder(const std::string& modelId, const std::filesystem::path& folderPath, std::string& errorMessage);
    bool verifyInstalledModel(const std::string& modelId, std::string& errorMessage);
    bool removeInstalledModel(const std::string& modelId, std::string& errorMessage);
    bool downloadModel(const std::string& modelId, std::string& errorMessage);
    bool updateModel(const std::string& modelId, std::string& errorMessage);
    bool cancelAllModelOperations(std::string* errorMessage = nullptr);
    bool prepareGenerationRuntime(std::string* errorMessage = nullptr, bool autoTriggered = false);
    bool cancelGenerationRuntimePreparation(std::string* errorMessage = nullptr);
    bool cancelTask(const std::string& jobId);
    std::filesystem::path modelsRootDirectory() const;
    RuntimeInstallDiagnostics runtimeInstallDiagnostics() const;
    bool revealRuntimeInstallLog() const;
    bool hasUnsavedChanges() const noexcept { return projectDirty_; }
    bool hasStalePreview() const noexcept { return previewPlaybackDirty_; }
    std::optional<std::string> projectFilePath() const;
    std::string backendStatusSummary() const;

    moon::engine::ProjectManager& projectManager() noexcept { return *projectManager_; }
    moon::engine::TimelineFacade& timeline() noexcept { return *timeline_; }
    moon::engine::TransportFacade& transport() noexcept { return *transport_; }
    moon::engine::WaveformService& waveformService() noexcept { return *waveformService_; }
    moon::engine::TaskManager& tasks() noexcept { return *taskManager_; }
    moon::engine::Logger& logger() noexcept { return *logger_; }

private:
    struct RuntimePrepareState;
    struct PreviewRenderState;

    std::unique_ptr<moon::engine::Logger> logger_;
    moon::engine::Settings settings_;
    moon::engine::HealthResponse backendHealth_;
    moon::engine::ModelsResponse backendModels_;
    bool backendFallbackNoticeActive_{false};
    bool autosaveRecoveryNoticeActive_{false};
    std::unique_ptr<moon::engine::SettingsService> settingsService_;
    std::unique_ptr<moon::engine::ProjectManager> projectManager_;
    std::unique_ptr<moon::engine::EngineRuntimeCoordinator> runtimeCoordinator_;
    std::unique_ptr<moon::engine::TimelineFacade> timeline_;
    std::unique_ptr<moon::engine::ClipOperations> clipOperations_;
    std::unique_ptr<moon::engine::TransportFacade> transport_;
    std::unique_ptr<moon::engine::WaveformService> waveformService_;
    std::unique_ptr<moon::engine::ExportService> exportService_;
    std::unique_ptr<moon::engine::JobClientProtocol> aiJobClient_;
    std::unique_ptr<BackendProcessManager> backendProcessManager_;
    std::unique_ptr<moon::engine::ModelManager> modelManager_;
    std::unique_ptr<moon::engine::TaskManager> taskManager_;
    std::unique_ptr<RuntimePrepareState> runtimePrepareState_;
    std::unique_ptr<PreviewRenderState> previewRenderState_;
    moon::engine::ModelRegistrySnapshot modelRegistrySnapshot_;
    bool previewPlaybackActive_{false};
    bool previewPlaybackDirty_{true};
    bool projectDirty_{false};
    bool interactiveTimelineEditActive_{false};
    bool interactiveTimelineEditWasPlaying_{false};
    double interactiveTimelineEditPlayheadSec_{0.0};
    bool runtimePreparePendingAutoStart_{false};
    double lastBackendStatusRefreshMs_{0.0};
    bool lastBackendReachableState_{false};
    std::uint64_t previewRenderGeneration_{0};
    std::filesystem::path previewMixPath_;
    std::string lastPlaybackRouteSummary_;
    std::string lastPlaybackDiagnostic_;
    std::string lastBackendStatusSummary_;
    std::string lastMusicGenerationError_;

    void markPreviewPlaybackDirty();
    void markProjectDirty();
    void clearProjectDirty();
    void refreshStartupNoticeState();
    void syncEngineIntegrationState(const std::string& reason = {});
    void logPlaybackRouteTransition(const std::string& reason);
    double currentTimelinePlayheadSec() const;
    bool finalizeTimelineEdit(const std::function<bool()>& editOperation,
                              bool syncTransportToSelectionAfterEdit = false,
                              const std::string& failureWarning = {},
                              const std::string& operationName = "timeline-edit");
    void restorePlaybackAfterTimelineEdit(bool wasPlaying, double timelinePlayheadSec);
    bool shouldUseProjectPreview() const;
    bool shouldUseLiveProjectPlayback() const;
    bool ensureProjectPlaybackRoute();
    void requestPreviewPlaybackRender();
    bool pollPreviewPlaybackRender();
    bool preparePreviewPlayback();
    bool ensureManagedBackendReady(std::string* errorMessage = nullptr, bool forceStart = false);
    bool pollRuntimePreparation(std::string* errorMessage = nullptr);
    std::optional<std::string> queueResolvedMusicGeneration(moon::engine::MusicGenerationRequest queuedRequest);
    bool pollPendingMusicGeneration();
    void applyRuntimeReadinessOverlay();

    std::optional<moon::engine::MusicGenerationRequest> pendingMusicGenerationRequest_;
    std::string pendingMusicGenerationTaskId_;
    std::string pendingMusicGenerationRealJobId_;
};
}
