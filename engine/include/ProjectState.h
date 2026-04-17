#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace moon::engine
{
struct AssetInfo
{
    std::string id;
    std::string path;
    bool generated{false};
    std::string sourceClipId;
    std::string modelName;
    std::string prompt;
    std::string createdAt;
    std::string cacheKey;
};

struct TrackInfo
{
    std::string id;
    std::string name;
    bool mute{false};
    bool solo{false};
    double gainDb{0.0};
    double pan{0.0};
};

struct ClipInfo
{
    std::string id;
    std::string trackId;
    std::string assetId;
    double startSec{0.0};
    double offsetSec{0.0};
    double durationSec{0.0};
    double gain{1.0};
    double fadeInSec{0.0};
    double fadeOutSec{0.0};
    std::string takeGroupId;
    bool activeTake{true};
    bool selected{false};
};

struct UIStateSnapshot
{
    double zoom{1.0};
    double playheadSec{0.0};
    std::string selectedClipId;
    std::string selectedTrackId;
    bool hasSelectedRegion{false};
    double selectedRegionStartSec{0.0};
    double selectedRegionEndSec{0.0};
};

struct EngineIntegrationState
{
    std::string timelineBackend{"lightweight"};
    std::string transportBackend{"lightweight"};
    std::string tracktionSyncState{"idle"};
    std::string tracktionSyncReason;
    bool tracktionRuntimeReady{false};
};

struct ProjectState
{
    int schemaVersion{1};
    std::string projectName{"Untitled"};
    int sampleRate{44100};
    double tempo{120.0};
    std::vector<TrackInfo> tracks;
    std::vector<ClipInfo> clips;
    std::unordered_map<std::string, AssetInfo> sourceAssets;
    std::unordered_map<std::string, AssetInfo> generatedAssets;
    UIStateSnapshot uiState;
    EngineIntegrationState engineState;
};
}
