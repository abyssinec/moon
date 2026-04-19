#include "SessionSerializer.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace moon::engine
{
bool SessionSerializer::save(const ProjectState& state, const std::filesystem::path& projectFile) const
{
    std::ofstream out(projectFile, std::ios::trunc);
    if (!out)
    {
        return false;
    }

    out << "{\n";
    out << "  \"schema_version\": " << state.schemaVersion << ",\n";
    out << "  \"project_name\": \"" << escape(state.projectName) << "\",\n";
    out << "  \"sample_rate\": " << state.sampleRate << ",\n";
    out << "  \"tempo\": " << state.tempo << ",\n";
    out << "  \"time_signature_numerator\": " << state.timeSignatureNumerator << ",\n";
    out << "  \"time_signature_denominator\": " << state.timeSignatureDenominator << ",\n";
    out << "  \"assets\": {\n";
    out << "    \"sources\": [\n";

    bool firstAsset = true;
    for (const auto& [id, asset] : state.sourceAssets)
    {
        if (!firstAsset)
        {
            out << ",\n";
        }
        out << "      {\"id\": \"" << escape(id) << "\", \"path\": \"" << escape(makeRelativePath(projectFile, asset.path)) << "\"}";
        firstAsset = false;
    }

    out << "\n    ],\n";
    out << "    \"generated\": [\n";

    firstAsset = true;
    for (const auto& [id, asset] : state.generatedAssets)
    {
        if (!firstAsset)
        {
            out << ",\n";
        }
        out << "      {\"id\": \"" << escape(id) << "\", \"path\": \"" << escape(makeRelativePath(projectFile, asset.path))
            << "\", \"source_clip_id\": \"" << escape(asset.sourceClipId)
            << "\", \"model\": \"" << escape(asset.modelName)
            << "\", \"prompt\": \"" << escape(asset.prompt)
            << "\", \"created_at\": \"" << escape(asset.createdAt)
            << "\", \"cache_key\": \"" << escape(asset.cacheKey) << "\"}";
        firstAsset = false;
    }

    out << "\n    ]\n";
    out << "  },\n";
    out << "  \"tracks\": [\n";

    for (std::size_t i = 0; i < state.tracks.size(); ++i)
    {
        const auto& track = state.tracks[i];
        out << "    {\"id\": \"" << escape(track.id)
            << "\", \"name\": \"" << escape(track.name)
            << "\", \"color\": \"" << escape(track.colorHex)
            << "\", \"mute\": " << (track.mute ? "true" : "false")
            << ", \"solo\": " << (track.solo ? "true" : "false")
            << ", \"gain_db\": " << track.gainDb
            << ", \"pan\": " << track.pan
            << "}";
        if (i + 1 < state.tracks.size())
        {
            out << ",";
        }
        out << "\n";
    }

    out << "  ],\n";
    out << "  \"clips\": [\n";

    for (std::size_t i = 0; i < state.clips.size(); ++i)
    {
        const auto& clip = state.clips[i];
        out << "    {\"id\": \"" << escape(clip.id) << "\", \"track_id\": \"" << escape(clip.trackId)
            << "\", \"asset_id\": \"" << escape(clip.assetId) << "\", \"start_sec\": " << clip.startSec
            << ", \"offset_sec\": " << clip.offsetSec << ", \"duration_sec\": " << clip.durationSec
            << ", \"gain\": " << clip.gain
            << ", \"fade_in_sec\": " << clip.fadeInSec
            << ", \"fade_out_sec\": " << clip.fadeOutSec
            << ", \"take_group_id\": \"" << escape(clip.takeGroupId) << "\""
            << ", \"active_take\": " << (clip.activeTake ? "true" : "false") << "}";
        if (i + 1 < state.clips.size())
        {
            out << ",";
        }
        out << "\n";
    }

    out << "  ],\n";
    out << "  \"engine_state\": {\n";
    out << "    \"timeline_backend\": \"" << escape(state.engineState.timelineBackend) << "\",\n";
    out << "    \"transport_backend\": \"" << escape(state.engineState.transportBackend) << "\",\n";
    out << "    \"tracktion_sync_state\": \"" << escape(state.engineState.tracktionSyncState) << "\",\n";
    out << "    \"tracktion_sync_reason\": \"" << escape(state.engineState.tracktionSyncReason) << "\",\n";
    out << "    \"tracktion_runtime_ready\": " << (state.engineState.tracktionRuntimeReady ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"ui_state\": {\n";
    out << "    \"zoom\": " << state.uiState.zoom << ",\n";
    out << "    \"playhead_sec\": " << state.uiState.playheadSec << ",\n";
    out << "    \"selected_clip_id\": \"" << escape(state.uiState.selectedClipId) << "\",\n";
    out << "    \"selected_track_id\": \"" << escape(state.uiState.selectedTrackId) << "\",\n";
    out << "    \"has_selected_region\": " << (state.uiState.hasSelectedRegion ? "true" : "false") << ",\n";
    out << "    \"selected_region_start_sec\": " << state.uiState.selectedRegionStartSec << ",\n";
    out << "    \"selected_region_end_sec\": " << state.uiState.selectedRegionEndSec << "\n";
    out << "  }\n";
    out << "}\n";
    return true;
}

std::optional<ProjectState> SessionSerializer::load(const std::filesystem::path& projectFile) const
{
    std::ifstream in(projectFile);
    if (!in)
    {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto content = buffer.str();

    ProjectState state;
    state.schemaVersion = extractInt(content, "schema_version", 1);
    state.projectName = extractString(content, "project_name", "Untitled");
    state.sampleRate = extractInt(content, "sample_rate", 44100);
    state.tempo = extractDouble(content, "tempo", 120.0);
    state.timeSignatureNumerator = extractInt(content, "time_signature_numerator", 4);
    state.timeSignatureDenominator = extractInt(content, "time_signature_denominator", 4);
    state.uiState.zoom = extractDouble(content, "zoom", 1.0);
    state.uiState.playheadSec = extractDouble(content, "playhead_sec", 0.0);
    state.uiState.selectedClipId = extractString(content, "selected_clip_id", {});
    state.uiState.selectedTrackId = extractString(content, "selected_track_id", {});
    state.uiState.hasSelectedRegion = extractBool(content, "has_selected_region", false);
    state.uiState.selectedRegionStartSec = extractDouble(content, "selected_region_start_sec", 0.0);
    state.uiState.selectedRegionEndSec = extractDouble(content, "selected_region_end_sec", 0.0);
    state.engineState.timelineBackend = extractString(content, "timeline_backend", "lightweight");
    state.engineState.transportBackend = extractString(content, "transport_backend", "lightweight");
    state.engineState.tracktionSyncState = extractString(content, "tracktion_sync_state", "idle");
    state.engineState.tracktionSyncReason = extractString(content, "tracktion_sync_reason", {});
    state.engineState.tracktionRuntimeReady = extractBool(content, "tracktion_runtime_ready", false);

    const auto tracksArray = extractArray(content, "tracks");
    const std::regex trackExpr("\\{\"id\"\\s*:\\s*\"([^\"]+)\",\\s*\"name\"\\s*:\\s*\"([^\"]*)\"(?:,\\s*\"color\"\\s*:\\s*\"([^\"]*)\")?(?:,\\s*\"mute\"\\s*:\\s*(true|false),\\s*\"solo\"\\s*:\\s*(true|false))?,\\s*\"gain_db\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?),\\s*\"pan\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\}");
    for (std::sregex_iterator it(tracksArray.begin(), tracksArray.end(), trackExpr), end; it != end; ++it)
    {
        TrackInfo track;
        track.id = (*it)[1].str();
        track.name = (*it)[2].str();
        if ((*it)[3].matched)
        {
            track.colorHex = (*it)[3].str();
        }
        if ((*it)[4].matched)
        {
            track.mute = ((*it)[4].str() == "true");
        }
        if ((*it)[5].matched)
        {
            track.solo = ((*it)[5].str() == "true");
        }
        if ((*it)[6].matched)
        {
            track.gainDb = std::stod((*it)[6].str());
        }
        if ((*it)[7].matched)
        {
            track.pan = std::stod((*it)[7].str());
        }
        state.tracks.push_back(track);
    }

    const auto clipsArray = extractArray(content, "clips");
    const std::regex clipExpr("\\{\"id\"\\s*:\\s*\"([^\"]+)\",\\s*\"track_id\"\\s*:\\s*\"([^\"]+)\",\\s*\"asset_id\"\\s*:\\s*\"([^\"]+)\",\\s*\"start_sec\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?),\\s*\"offset_sec\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?),\\s*\"duration_sec\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?),\\s*\"gain\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?)(?:,\\s*\"fade_in_sec\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?),\\s*\"fade_out_sec\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?),\\s*\"take_group_id\"\\s*:\\s*\"([^\"]*)\",\\s*\"active_take\"\\s*:\\s*(true|false))?\\}");
    for (std::sregex_iterator it(clipsArray.begin(), clipsArray.end(), clipExpr), end; it != end; ++it)
    {
        ClipInfo clip;
        clip.id = (*it)[1].str();
        clip.trackId = (*it)[2].str();
        clip.assetId = (*it)[3].str();
        clip.startSec = std::stod((*it)[4].str());
        clip.offsetSec = std::stod((*it)[5].str());
        clip.durationSec = std::stod((*it)[6].str());
        clip.gain = std::stod((*it)[7].str());
        if ((*it)[8].matched)
        {
            clip.fadeInSec = std::stod((*it)[8].str());
        }
        if ((*it)[9].matched)
        {
            clip.fadeOutSec = std::stod((*it)[9].str());
        }
        if ((*it)[10].matched)
        {
            clip.takeGroupId = (*it)[10].str();
        }
        if ((*it)[11].matched)
        {
            clip.activeTake = ((*it)[11].str() == "true");
        }
        clip.selected = (clip.id == state.uiState.selectedClipId);
        state.clips.push_back(clip);
    }

    const auto sourcesArray = extractArray(content, "sources");
    const std::regex assetExpr("\\{\"id\"\\s*:\\s*\"([^\"]+)\",\\s*\"path\"\\s*:\\s*\"([^\"]*)\"\\}");
    for (std::sregex_iterator it(sourcesArray.begin(), sourcesArray.end(), assetExpr), end; it != end; ++it)
    {
        AssetInfo asset;
        asset.id = (*it)[1].str();
        asset.path = makeAbsolutePath(projectFile, (*it)[2].str());
        asset.generated = false;
        state.sourceAssets.emplace(asset.id, asset);
    }

    const auto generatedArray = extractArray(content, "generated");
    const std::regex generatedExpr("\\{\"id\"\\s*:\\s*\"([^\"]+)\",\\s*\"path\"\\s*:\\s*\"([^\"]*)\",\\s*\"source_clip_id\"\\s*:\\s*\"([^\"]*)\",\\s*\"model\"\\s*:\\s*\"([^\"]*)\",\\s*\"prompt\"\\s*:\\s*\"([^\"]*)\",\\s*\"created_at\"\\s*:\\s*\"([^\"]*)\",\\s*\"cache_key\"\\s*:\\s*\"([^\"]*)\"\\}");
    for (std::sregex_iterator it(generatedArray.begin(), generatedArray.end(), generatedExpr), end; it != end; ++it)
    {
        AssetInfo asset;
        asset.id = (*it)[1].str();
        asset.path = makeAbsolutePath(projectFile, (*it)[2].str());
        asset.sourceClipId = (*it)[3].str();
        asset.modelName = (*it)[4].str();
        asset.prompt = (*it)[5].str();
        asset.createdAt = (*it)[6].str();
        asset.cacheKey = (*it)[7].str();
        asset.generated = true;
        state.generatedAssets.emplace(asset.id, asset);
    }

    return state;
}

std::string SessionSerializer::makeRelativePath(const std::filesystem::path& projectFile, const std::string& path)
{
    if (path.empty())
    {
        return {};
    }

    const auto target = std::filesystem::path(path);
    if (!target.is_absolute())
    {
        return target.lexically_normal().string();
    }

    const auto root = projectFile.parent_path();
    std::error_code ec;
    const auto relative = std::filesystem::relative(target, root, ec);
    return ec ? target.lexically_normal().string() : relative.lexically_normal().string();
}

std::string SessionSerializer::makeAbsolutePath(const std::filesystem::path& projectFile, const std::string& path)
{
    if (path.empty())
    {
        return {};
    }

    const auto candidate = std::filesystem::path(path);
    if (candidate.is_absolute())
    {
        return candidate.lexically_normal().string();
    }

    return (projectFile.parent_path() / candidate).lexically_normal().string();
}

std::string SessionSerializer::escape(const std::string& text)
{
    std::string output;
    output.reserve(text.size());
    for (const auto ch : text)
    {
        if (ch == '\"')
        {
            output += "\\\"";
        }
        else if (ch == '\\')
        {
            output += "\\\\";
        }
        else
        {
            output += ch;
        }
    }
    return output;
}

std::string SessionSerializer::extractString(const std::string& content, const std::string& key, const std::string& fallback)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return match[1].str();
    }
    return fallback;
}

double SessionSerializer::extractDouble(const std::string& content, const std::string& key, double fallback)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return std::stod(match[1].str());
    }
    return fallback;
}

int SessionSerializer::extractInt(const std::string& content, const std::string& key, int fallback)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return std::stoi(match[1].str());
    }
    return fallback;
}

bool SessionSerializer::extractBool(const std::string& content, const std::string& key, bool fallback)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return match[1].str() == "true";
    }
    return fallback;
}

std::string SessionSerializer::extractArray(const std::string& content, const std::string& key)
{
    const auto keyPos = content.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
    {
        return {};
    }

    const auto start = content.find('[', keyPos);
    if (start == std::string::npos)
    {
        return {};
    }

    int depth = 0;
    for (std::size_t i = start; i < content.size(); ++i)
    {
        if (content[i] == '[')
        {
            ++depth;
        }
        else if (content[i] == ']')
        {
            --depth;
            if (depth == 0)
            {
                return content.substr(start, i - start + 1);
            }
        }
    }

    return {};
}
}

