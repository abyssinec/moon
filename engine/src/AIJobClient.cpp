#include "AIJobClient.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include "FileHash.h"

namespace moon::engine
{
namespace
{
void writeStubWav(const std::filesystem::path& outputPath, double durationSec)
{
    std::filesystem::create_directories(outputPath.parent_path());

    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint32_t sampleRate = 44100;
    const auto frameCount = static_cast<std::uint32_t>(std::max(1.0, durationSec) * sampleRate);
    const std::uint32_t blockAlign = channels * (bitsPerSample / 8);
    const std::uint32_t byteRate = sampleRate * blockAlign;
    const std::uint32_t dataSize = frameCount * blockAlign;
    const std::uint32_t riffSize = 36 + dataSize;

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return;
    }

    const auto writeBytes = [&out](const auto& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };

    out.write("RIFF", 4);
    writeBytes(riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);

    const std::uint32_t fmtChunkSize = 16;
    const std::uint16_t audioFormat = 1;
    writeBytes(fmtChunkSize);
    writeBytes(audioFormat);
    writeBytes(channels);
    writeBytes(sampleRate);
    writeBytes(byteRate);
    writeBytes(static_cast<std::uint16_t>(blockAlign));
    writeBytes(bitsPerSample);

    out.write("data", 4);
    writeBytes(dataSize);

    const std::int16_t silentSample = 0;
    for (std::uint32_t i = 0; i < frameCount * channels; ++i)
    {
        writeBytes(silentSample);
    }
}

std::wstring widen(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

std::string jsonEscape(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    for (const auto ch : text)
    {
        if (ch == '\\')
        {
            result += "\\\\";
        }
        else if (ch == '\"')
        {
            result += "\\\"";
        }
        else
        {
            result += ch;
        }
    }
    return result;
}

std::string buildMusicPrompt(const MusicGenerationRequest& request)
{
    std::string prompt = std::string(musicGenerationCategoryLabel(request.category));
    if (!request.stylesPrompt.empty())
    {
        prompt += ": ";
        prompt += request.stylesPrompt;
    }
    if (!request.secondaryPrompt.empty() && !request.secondaryPromptIsLyrics)
    {
        prompt += " | ";
        prompt += request.secondaryPrompt;
    }
    return prompt;
}

std::string devicePreferenceValue(ComputeDevicePreference preference)
{
    switch (preference)
    {
    case ComputeDevicePreference::GPU:  return "gpu";
    case ComputeDevicePreference::CPU:  return "cpu";
    case ComputeDevicePreference::Auto: return "auto";
    }

    return "auto";
}

struct BackendEndpoint
{
    std::wstring host{L"127.0.0.1"};
    INTERNET_PORT port{8000};
    std::wstring basePath;
};

BackendEndpoint parseBackendEndpoint(const std::string& backendUrl)
{
    BackendEndpoint endpoint;
    std::string remaining = backendUrl;
    if (remaining.rfind("http://", 0) == 0)
    {
        remaining = remaining.substr(7);
    }
    else if (remaining.rfind("https://", 0) == 0)
    {
        remaining = remaining.substr(8);
    }

    auto slashPos = remaining.find('/');
    std::string authority = slashPos == std::string::npos ? remaining : remaining.substr(0, slashPos);
    std::string basePath = slashPos == std::string::npos ? "" : remaining.substr(slashPos);

    auto colonPos = authority.rfind(':');
    if (colonPos != std::string::npos)
    {
        endpoint.host = widen(authority.substr(0, colonPos));
        const auto portText = authority.substr(colonPos + 1);
        const auto parsedPort = std::stoi(portText);
        endpoint.port = static_cast<INTERNET_PORT>(std::clamp(parsedPort, 1, 65535));
    }
    else if (!authority.empty())
    {
        endpoint.host = widen(authority);
    }

    if (!basePath.empty() && basePath != "/")
    {
        endpoint.basePath = widen(basePath);
    }
    return endpoint;
}

std::wstring combineRequestPath(const BackendEndpoint& endpoint, const std::string& path)
{
    std::wstring requestPath = endpoint.basePath.empty() ? L"" : endpoint.basePath;
    if (requestPath.empty())
    {
        requestPath = widen(path);
    }
    else
    {
        if (requestPath.back() == L'/' && !path.empty() && path.front() == '/')
        {
            requestPath.pop_back();
        }
        requestPath += widen(path);
    }
    return requestPath;
}
}

AIJobClient::AIJobClient(std::string backendUrl, Logger& logger)
    : backendUrl_(std::move(backendUrl))
    , logger_(logger)
{
}

void AIJobClient::setBackendUrl(std::string backendUrl)
{
    backendUrl_ = std::move(backendUrl);
    backendReachable_ = false;
}

HealthResponse AIJobClient::healthCheck() const
{
    if (const auto response = httpGet("/health"); response.has_value())
    {
        markBackendReachable();
        return HealthResponse{
            extractJsonString(*response, "status").value_or("ok"),
            extractJsonString(*response, "backend").value_or("local-ai-audio-service")};
    }

    markBackendFallback();
    return HealthResponse{"ok", "local-ai-audio-service"};
}

ModelsResponse AIJobClient::models() const
{
    if (const auto response = httpGet("/models"); response.has_value())
    {
        markBackendReachable();
        ModelsResponse result;
        if (response->find("demucs") != std::string::npos)
        {
            result.stems.push_back("demucs");
        }
        if (response->find("ace_step_stub") != std::string::npos)
        {
            result.rewrite.push_back("ace_step_stub");
            result.addLayer.push_back("ace_step_stub");
        }
        if (response->find("ace_step") != std::string::npos)
        {
            result.musicGeneration.push_back("ace_step");
        }
        else if (response->find("ace_step_stub") != std::string::npos)
        {
            result.musicGeneration.push_back("ace_step_stub");
        }
        if (!result.stems.empty() || !result.rewrite.empty() || !result.addLayer.empty())
        {
            return result;
        }
    }

    markBackendFallback();
    return ModelsResponse{{"demucs"}, {"ace_step_stub"}, {"ace_step_stub"}, {"ace_step_stub"}};
}

std::string AIJobClient::createStemsJob(const std::string& inputAudioPath, const std::string& modelName)
{
    std::ostringstream body;
    body << "{\"input_audio_path\":\"" << jsonEscape(inputAudioPath) << "\",\"model\":\"" << jsonEscape(modelName) << "\"}";
    if (const auto response = httpPost("/jobs/stems", body.str()); response.has_value())
    {
        markBackendReachable();
        if (const auto id = extractJsonString(*response, "id"); id.has_value())
        {
            logger_.info("Created backend stems job " + *id);
            return *id;
        }
    }

    markBackendFallback();
    return createJob("stems", inputAudioPath, modelName, "", 0.0);
}

std::string AIJobClient::createRewriteJob(const std::string& inputAudioPath,
                                          const std::string& prompt,
                                          const std::string& modelName,
                                          double durationSec)
{
    std::ostringstream body;
    body << "{\"input_audio_path\":\"" << jsonEscape(inputAudioPath)
         << "\",\"prompt\":\"" << jsonEscape(prompt)
         << "\",\"model\":\"" << jsonEscape(modelName)
         << "\",\"duration_sec\":" << durationSec
         << ",\"strength\":0.55,\"preserve_timing\":true,\"preserve_melody\":true,\"seed\":0}";
    if (const auto response = httpPost("/jobs/rewrite", body.str()); response.has_value())
    {
        markBackendReachable();
        if (const auto id = extractJsonString(*response, "id"); id.has_value())
        {
            logger_.info("Created backend rewrite job " + *id);
            return *id;
        }
    }

    markBackendFallback();
    return createJob("rewrite", inputAudioPath, modelName, prompt, durationSec);
}

std::string AIJobClient::createAddLayerJob(const std::string& inputAudioPath,
                                           const std::string& prompt,
                                           const std::string& modelName,
                                           double durationSec)
{
    std::ostringstream body;
    body << "{\"input_audio_path\":\"" << jsonEscape(inputAudioPath)
         << "\",\"prompt\":\"" << jsonEscape(prompt)
         << "\",\"model\":\"" << jsonEscape(modelName)
         << "\",\"duration_sec\":" << durationSec
         << ",\"seed\":0}";
    if (const auto response = httpPost("/jobs/add-layer", body.str()); response.has_value())
    {
        markBackendReachable();
        if (const auto id = extractJsonString(*response, "id"); id.has_value())
        {
            logger_.info("Created backend add-layer job " + *id);
            return *id;
        }
    }

    markBackendFallback();
    return createJob("add-layer", inputAudioPath, modelName, prompt, durationSec);
}

std::string AIJobClient::createMusicGenerationJob(const MusicGenerationRequest& request)
{
    const auto prompt = buildMusicPrompt(request);
    std::ostringstream body;
    body << "{"
         << "\"model\":\"" << jsonEscape(request.selectedModel.empty() ? "ace_step" : request.selectedModel) << "\","
         << "\"checkpoint_path\":\"" << jsonEscape(request.selectedModelPath) << "\","
         << "\"output_path\":\"\","
         << "\"prompt\":\"" << jsonEscape(prompt) << "\","
         << "\"lyrics\":\"" << jsonEscape(request.lyricsPrompt) << "\","
         << "\"notes\":\"" << jsonEscape(request.secondaryPromptIsLyrics ? std::string{} : request.secondaryPrompt) << "\","
         << "\"category\":\"" << jsonEscape(std::string(musicGenerationCategoryLabel(request.category))) << "\","
         << "\"duration_sec\":" << std::max(1.0, request.durationSec) << ","
         << "\"seed\":" << request.seed << ","
         << "\"device\":\"" << jsonEscape(devicePreferenceValue(request.devicePreference)) << "\","
         << "\"bpm\":" << request.bpm << ","
         << "\"musical_key\":\"" << jsonEscape(request.musicalKey) << "\""
         << "}";
    if (const auto response = httpPost("/jobs/music-generation", body.str()); response.has_value())
    {
        markBackendReachable();
        if (const auto id = extractJsonString(*response, "id"); id.has_value())
        {
            logger_.info("Created backend music generation job " + *id);
            return *id;
        }
    }

    markBackendFallback();
    return createJob("music-generation", "", request.selectedModel.empty() ? "ace_step_stub" : request.selectedModel, prompt, request.durationSec);
}

JobStatusResponse AIJobClient::getJob(const std::string& jobId)
{
    if (const auto response = httpGet("/jobs/" + jobId); response.has_value())
    {
        markBackendReachable();
        JobStatusResponse status;
        status.id = extractJsonString(*response, "id").value_or(jobId);
        status.type = extractJsonString(*response, "type").value_or("unknown");
        status.status = extractJsonString(*response, "status").value_or("queued");
        status.progress = extractJsonDouble(*response, "progress").value_or(0.0);
        status.message = extractJsonString(*response, "message").value_or("processing");
        return status;
    }

    auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return JobStatusResponse{jobId, "unknown", "failed", 1.0, "job not found"};
    }

    markBackendFallback();
    auto& job = it->second;
    if (job.status.status != "completed")
    {
        job.status.status = "running";
        job.status.progress = std::min(1.0, job.status.progress + 0.35);
        job.status.message = "processing";

        if (job.status.progress >= 1.0)
        {
            job.status.status = "completed";
            job.status.message = "completed";
            job.result.status = "completed";
        }
    }

    return job.status;
}

JobResultResponse AIJobClient::getJobResult(const std::string& jobId) const
{
    if (const auto response = httpGet("/jobs/" + jobId + "/result"); response.has_value())
    {
        markBackendReachable();
        JobResultResponse result;
        result.id = extractJsonString(*response, "id").value_or(jobId);
        result.status = extractJsonString(*response, "status").value_or("completed");
        result.outputAudioPath = extractJsonString(*response, "output_audio_path").value_or("");
        result.outputs = extractJsonObjectStrings(*response, "outputs");
        return result;
    }

    const auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return JobResultResponse{jobId, "failed", {}, {}};
    }
    markBackendFallback();
    return it->second.result;
}

bool AIJobClient::cancelJob(const std::string& jobId)
{
    if (const auto response = httpPost("/jobs/" + jobId + "/cancel", "{}"); response.has_value())
    {
        markBackendReachable();
        return true;
    }

    auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return false;
    }

    markBackendFallback();
    it->second.status.status = "cancelled";
    it->second.status.progress = 1.0;
    it->second.status.message = "cancelled";
    it->second.result.status = "cancelled";
    return true;
}

void AIJobClient::setBackendReachableHint(bool reachable) const noexcept
{
    backendReachable_ = reachable;
}

std::optional<std::string> AIJobClient::httpGet(const std::string& path) const
{
#if defined(_WIN32)
    const auto endpoint = parseBackendEndpoint(backendUrl_);
    const auto requestPath = combineRequestPath(endpoint, path);

    HINTERNET session = WinHttpOpen(L"MoonAudioEditor/0.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (session == nullptr)
    {
        return std::nullopt;
    }

    WinHttpSetTimeouts(session, 250, 250, 500, 500);

    HINTERNET connection = WinHttpConnect(session, endpoint.host.c_str(), endpoint.port, 0);
    if (connection == nullptr)
    {
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HINTERNET request = WinHttpOpenRequest(connection,
                                           L"GET",
                                           requestPath.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           0);
    if (request == nullptr)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    std::optional<std::string> result;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(request, nullptr))
    {
        std::string response;
        DWORD available = 0;
        do
        {
            available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
            {
                break;
            }

            std::string buffer(static_cast<std::size_t>(available), '\0');
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &downloaded))
            {
                response.clear();
                break;
            }
            buffer.resize(downloaded);
            response += buffer;
        } while (available > 0);

        if (!response.empty())
        {
            result = response;
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
#else
    (void) path;
    return std::nullopt;
#endif
}

std::optional<std::string> AIJobClient::httpPost(const std::string& path, const std::string& jsonBody) const
{
#if defined(_WIN32)
    const auto endpoint = parseBackendEndpoint(backendUrl_);
    const auto requestPath = combineRequestPath(endpoint, path);
    const auto headers = widen("Content-Type: application/json\r\n");

    HINTERNET session = WinHttpOpen(L"MoonAudioEditor/0.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (session == nullptr)
    {
        return std::nullopt;
    }

    WinHttpSetTimeouts(session, 250, 250, 500, 500);

    HINTERNET connection = WinHttpConnect(session, endpoint.host.c_str(), endpoint.port, 0);
    if (connection == nullptr)
    {
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HINTERNET request = WinHttpOpenRequest(connection,
                                           L"POST",
                                           requestPath.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           0);
    if (request == nullptr)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    std::optional<std::string> result;
    if (WinHttpSendRequest(request,
                           headers.c_str(),
                           static_cast<DWORD>(headers.size()),
                           const_cast<char*>(jsonBody.data()),
                           static_cast<DWORD>(jsonBody.size()),
                           static_cast<DWORD>(jsonBody.size()),
                           0)
        && WinHttpReceiveResponse(request, nullptr))
    {
        std::string response;
        DWORD available = 0;
        do
        {
            available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
            {
                break;
            }

            std::string buffer(static_cast<std::size_t>(available), '\0');
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &downloaded))
            {
                response.clear();
                break;
            }
            buffer.resize(downloaded);
            response += buffer;
        } while (available > 0);

        if (!response.empty())
        {
            result = response;
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
#else
    (void) path;
    (void) jsonBody;
    return std::nullopt;
#endif
}

std::optional<std::string> AIJobClient::extractJsonString(const std::string& json, const std::string& key)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, expr) && match.size() > 1)
    {
        return match[1].str();
    }
    return std::nullopt;
}

std::optional<double> AIJobClient::extractJsonDouble(const std::string& json, const std::string& key)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(json, match, expr) && match.size() > 1)
    {
        return std::stod(match[1].str());
    }
    return std::nullopt;
}

std::unordered_map<std::string, std::string> AIJobClient::extractJsonObjectStrings(const std::string& json, const std::string& key)
{
    std::unordered_map<std::string, std::string> result;
    const auto keyPos = json.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
    {
        return result;
    }

    const auto objectStart = json.find('{', keyPos);
    if (objectStart == std::string::npos)
    {
        return result;
    }

    int depth = 0;
    std::size_t objectEnd = std::string::npos;
    for (std::size_t i = objectStart; i < json.size(); ++i)
    {
        if (json[i] == '{')
        {
            ++depth;
        }
        else if (json[i] == '}')
        {
            --depth;
            if (depth == 0)
            {
                objectEnd = i;
                break;
            }
        }
    }

    if (objectEnd == std::string::npos)
    {
        return result;
    }

    const auto objectJson = json.substr(objectStart, objectEnd - objectStart + 1);
    const std::regex pairExpr("\"([^\"]+)\"\\s*:\\s*\"([^\"]*)\"");
    for (std::sregex_iterator it(objectJson.begin(), objectJson.end(), pairExpr), end; it != end; ++it)
    {
        result.emplace((*it)[1].str(), (*it)[2].str());
    }
    return result;
}

std::string AIJobClient::createJob(const std::string& type,
                                   const std::string& inputAudioPath,
                                   const std::string& modelName,
                                   const std::string& prompt,
                                   double durationSec)
{
    const auto id = "job-" + std::to_string(nextJobId_++);
    const auto cacheKey = FileHash::hashPath(inputAudioPath + "|" + modelName + "|" + prompt);
    const auto basePath = std::filesystem::path("cache") / cacheKey;

    LocalJob job;
    job.status = JobStatusResponse{id, type, "queued", 0.0, "queued"};
    job.result.id = id;

    if (type == "stems")
    {
        job.result.outputs = {
            {"vocals", (basePath.string() + "_vocals.wav")},
            {"drums", (basePath.string() + "_drums.wav")},
            {"bass", (basePath.string() + "_bass.wav")},
            {"other", (basePath.string() + "_other.wav")},
        };
        for (const auto& [_, path] : job.result.outputs)
        {
            writeStubWav(path, 10.0);
        }
    }
    else
    {
        const auto suffix = type == "rewrite" ? "_rewrite.wav" : (type == "music-generation" ? "_music.wav" : "_layer.wav");
        job.result.outputAudioPath = basePath.string() + suffix;
        writeStubWav(job.result.outputAudioPath, std::max(0.1, durationSec));
    }

    jobs_.emplace(id, job);
    logger_.info("Created local stub job " + id + " for " + type + " duration=" + std::to_string(durationSec));
    return id;
}

void AIJobClient::markBackendReachable() const noexcept
{
    backendReachable_ = true;
}

void AIJobClient::markBackendFallback() const noexcept
{
    backendReachable_ = false;
}
}

