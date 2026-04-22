#include "LocalJobClient.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

#if MOON_HAS_JUCE
#include <juce_core/juce_core.h>
#endif

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

bool envPresent(const char* key)
{
    const auto* value = std::getenv(key);
    return value != nullptr && *value != '\0';
}

std::string envValue(const char* key)
{
    const auto* value = std::getenv(key);
    return value != nullptr ? std::string(value) : std::string{};
}

std::string trimCopy(std::string value)
{
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
    return value;
}

std::string buildMusicPrompt(const MusicGenerationRequest& request)
{
    std::ostringstream prompt;
    prompt << musicGenerationCategoryLabel(request.category);
    if (!request.stylesPrompt.empty())
    {
        prompt << " | " << request.stylesPrompt;
    }
    if (!request.isInstrumental && !request.lyricsPrompt.empty())
    {
        prompt << " | vocals";
    }
    if (!request.secondaryPrompt.empty() && !request.secondaryPromptIsLyrics)
    {
        prompt << " | notes: " << request.secondaryPrompt;
    }
    if (request.bpm > 0.0)
    {
        prompt << " | bpm " << static_cast<int>(std::round(request.bpm));
    }
    if (!request.musicalKey.empty())
    {
        prompt << " | key " << request.musicalKey;
    }
    return prompt.str();
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

bool pathLooksUsable(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

#if defined(_WIN32)
struct BackendEndpoint
{
    std::wstring host{L"127.0.0.1"};
    INTERNET_PORT port{8001};
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
        endpoint.port = static_cast<INTERNET_PORT>(std::clamp(std::stoi(portText), 1, 65535));
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

std::optional<std::string> httpGet(const std::string& backendUrl, const std::string& path)
{
    const auto endpoint = parseBackendEndpoint(backendUrl);
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

    WinHttpSetTimeouts(session, 250, 250, 750, 750);
    HINTERNET connection = WinHttpConnect(session, endpoint.host.c_str(), endpoint.port, 0);
    if (connection == nullptr)
    {
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
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
}

std::optional<std::string> httpPost(const std::string& backendUrl, const std::string& path, const std::string& jsonBody)
{
    const auto endpoint = parseBackendEndpoint(backendUrl);
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

    WinHttpSetTimeouts(session, 250, 250, 2000, 2000);
    HINTERNET connection = WinHttpConnect(session, endpoint.host.c_str(), endpoint.port, 0);
    if (connection == nullptr)
    {
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"POST", requestPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
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
}

std::optional<std::string> extractJsonString(const std::string& json, const std::string& key)
{
    const auto keyToken = "\"" + key + "\"";
    const auto keyPos = json.find(keyToken);
    if (keyPos == std::string::npos)
    {
        return std::nullopt;
    }

    const auto colon = json.find(':', keyPos + keyToken.size());
    const auto firstQuote = json.find('\"', colon + 1);
    const auto secondQuote = json.find('\"', firstQuote + 1);
    if (colon == std::string::npos || firstQuote == std::string::npos || secondQuote == std::string::npos)
    {
        return std::nullopt;
    }

    return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}
#endif
}

LocalJobClient::LocalJobClient(std::string backendUrl, Logger& logger)
    : backendUrl_(std::move(backendUrl))
    , logger_(logger)
{
}

LocalJobClient::~LocalJobClient()
{
    shuttingDown_.store(true);
    for (auto& worker : workerThreads_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

void LocalJobClient::setBackendUrl(std::string backendUrl)
{
    backendUrl_ = std::move(backendUrl);
}

HealthResponse LocalJobClient::healthCheck() const
{
    return {"ok", "local-job-service"};
}

ModelsResponse LocalJobClient::models() const
{
    return detectModels();
}

std::string LocalJobClient::createStemsJob(const std::string& inputAudioPath, const std::string& modelName)
{
    return createJob("stems", inputAudioPath, modelName, "", 0.0);
}

std::string LocalJobClient::createRewriteJob(const std::string& inputAudioPath,
                                             const std::string& prompt,
                                             const std::string& modelName,
                                             double durationSec)
{
    return createJob("rewrite", inputAudioPath, modelName, prompt, durationSec);
}

std::string LocalJobClient::createAddLayerJob(const std::string& inputAudioPath,
                                              const std::string& prompt,
                                              const std::string& modelName,
                                              double durationSec)
{
    return createJob("add-layer", inputAudioPath, modelName, prompt, durationSec);
}

std::string LocalJobClient::createMusicGenerationJob(const MusicGenerationRequest& request)
{
    const auto id = "job-" + std::to_string(nextJobId_++);
    const auto prompt = buildMusicPrompt(request);
    const auto cacheKey = FileHash::hashPath(
        std::string(musicGenerationCategoryLabel(request.category))
        + "|" + request.stylesPrompt + "|" + request.secondaryPrompt + "|" + request.lyricsPrompt + "|"
        + request.selectedModel + "|" + request.selectedModelVersion + "|" + std::to_string(request.durationSec) + "|"
        + std::to_string(request.seed) + "|" + devicePreferenceValue(request.devicePreference));
    const auto outputPath = (std::filesystem::path("cache") / (cacheKey + "_music.wav")).string();

    LocalJob job;
    job.status = JobStatusResponse{id, "music-generation", "queued", 0.0, "queued"};
    job.result = JobResultResponse{id, "queued", {}, outputPath};
    {
        std::scoped_lock lock(jobsMutex_);
        jobs_.emplace(id, job);
    }

    workerThreads_.emplace_back([this, jobId = id, jobRequest = request, outputAudioPath = outputPath]()
    {
        runMusicGenerationJob(jobId, jobRequest, outputAudioPath);
    });

    logger_.info("Queued music generation job " + id + " model=" + (request.selectedModel.empty() ? std::string("auto") : request.selectedModel));
    return id;
}

JobStatusResponse LocalJobClient::getJob(const std::string& jobId)
{
    std::scoped_lock lock(jobsMutex_);
    auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return JobStatusResponse{jobId, "unknown", "failed", 1.0, "job not found"};
    }

    auto& job = it->second;
    if (job.status.type == "music-generation")
    {
        return job.status;
    }

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

JobResultResponse LocalJobClient::getJobResult(const std::string& jobId) const
{
    std::scoped_lock lock(jobsMutex_);
    const auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return JobResultResponse{jobId, "failed", {}, {}};
    }
    return it->second.result;
}

bool LocalJobClient::cancelJob(const std::string& jobId)
{
    std::scoped_lock lock(jobsMutex_);
    const auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return false;
    }

    it->second.status.status = "cancelled";
    it->second.status.progress = 1.0;
    it->second.status.message = "cancelled";
    it->second.result.status = "cancelled";
    it->second.result.outputAudioPath.clear();
    return true;
}

std::string LocalJobClient::createJob(const std::string& type,
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
        const auto suffix = type == "rewrite" ? "_rewrite.wav" : "_layer.wav";
        job.result.outputAudioPath = basePath.string() + suffix;
        writeStubWav(job.result.outputAudioPath, std::max(0.1, durationSec));
    }

    std::scoped_lock lock(jobsMutex_);
    jobs_.emplace(id, job);
    logger_.info("Created local job " + id + " for " + type + " model=" + modelName);
    return id;
}

void LocalJobClient::runMusicGenerationJob(std::string jobId, MusicGenerationRequest request, std::string outputAudioPath)
{
    auto updateStatus = [this, &jobId](const std::string& status, double progress, const std::string& message)
    {
        std::scoped_lock lock(jobsMutex_);
        if (auto it = jobs_.find(jobId); it != jobs_.end())
        {
            it->second.status.status = status;
            it->second.status.progress = progress;
            it->second.status.message = message;
            it->second.result.status = status;
        }
    };

    auto completeFailure = [this, &jobId](const std::string& message)
    {
        std::scoped_lock lock(jobsMutex_);
        if (auto it = jobs_.find(jobId); it != jobs_.end())
        {
            it->second.status.status = "failed";
            it->second.status.progress = 1.0;
            it->second.status.message = message;
            it->second.result.status = "failed";
            it->second.result.outputAudioPath.clear();
        }
    };

    if (shuttingDown_.load())
    {
        completeFailure("shutdown");
        return;
    }

    updateStatus("running", 0.1, "preparing");
    const auto selectedModel = request.selectedModel.empty() ? "ace_step" : request.selectedModel;
    const auto prompt = buildMusicPrompt(request);
    const auto checkpointPath = trimCopy(!request.selectedModelPath.empty() ? request.selectedModelPath : envValue("MOON_ACE_STEP_CHECKPOINT_PATH"));
    const auto apiUrl = trimCopy(envValue("MOON_ACE_STEP_API_URL"));
    const auto executable = trimCopy(envValue("MOON_ACE_STEP_EXECUTABLE"));

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outputAudioPath).parent_path(), ec);

    try
    {
        if (!apiUrl.empty())
        {
#if defined(_WIN32)
            updateStatus("running", 0.45, "calling Acestep API");
            std::ostringstream body;
            body << "{"
                 << "\"checkpoint_path\":\"" << jsonEscape(checkpointPath) << "\","
                 << "\"output_path\":\"" << jsonEscape(outputAudioPath) << "\","
                 << "\"audio_duration\":" << std::max(1.0, request.durationSec) << ","
                 << "\"prompt\":\"" << jsonEscape(prompt) << "\","
                  << "\"lyrics\":\"" << jsonEscape((request.isInstrumental || !request.secondaryPromptIsLyrics) ? std::string{} : request.lyricsPrompt) << "\","
                  << "\"notes\":\"" << jsonEscape(request.secondaryPromptIsLyrics ? std::string{} : request.secondaryPrompt) << "\","
                  << "\"model_path\":\"" << jsonEscape(request.selectedModelPath) << "\","
                  << "\"device\":\"" << jsonEscape(devicePreferenceValue(request.devicePreference)) << "\","
                  << "\"actual_seeds\":[" << request.seed << "]"
                  << "}";
            const auto response = httpPost(apiUrl, "/generate", body.str());
            const auto resolvedOutput = response.has_value() ? extractJsonString(*response, "output_audio_path").value_or(outputAudioPath) : outputAudioPath;
            if (!response.has_value() || !std::filesystem::exists(resolvedOutput))
            {
                throw std::runtime_error("Acestep API did not produce an audio file");
            }
            outputAudioPath = resolvedOutput;
#else
            (void) selectedModel;
            throw std::runtime_error("Acestep API generation is only wired on Windows builds");
#endif
        }
        else
        {
            if (executable.empty())
            {
                throw std::runtime_error(
                    checkpointPath.empty()
                        ? "ACE-Step is not configured. Set MOON_ACE_STEP_API_URL or MOON_ACE_STEP_EXECUTABLE."
                        : "ACE-Step checkpoint is configured, but no runtime is configured. Set MOON_ACE_STEP_API_URL or MOON_ACE_STEP_EXECUTABLE.");
            }

            const auto command = executable;
            updateStatus("running", 0.45, "running Acestep");
            std::vector<std::string> arguments{
                command,
                "--output",
                outputAudioPath,
                "--prompt",
                prompt,
                "--duration",
                std::to_string(std::max(1.0, request.durationSec)),
                "--seed",
                std::to_string(request.seed),
            };
              if (!checkpointPath.empty())
              {
                  arguments.emplace_back("--checkpoint");
                  arguments.push_back(checkpointPath);
              }
              arguments.emplace_back("--device");
              arguments.push_back(devicePreferenceValue(request.devicePreference));
              if (!request.isInstrumental && request.secondaryPromptIsLyrics && !request.lyricsPrompt.empty())
            {
                arguments.emplace_back("--lyrics");
                arguments.push_back(request.lyricsPrompt);
            }
            if (!request.secondaryPromptIsLyrics && !request.secondaryPrompt.empty())
            {
                arguments.emplace_back("--notes");
                arguments.push_back(request.secondaryPrompt);
            }

#if MOON_HAS_JUCE
            juce::ChildProcess process;
            juce::StringArray commandLine;
            for (const auto& part : arguments)
            {
                commandLine.add(part);
            }

            if (!process.start(commandLine))
            {
                throw std::runtime_error("Unable to launch Acestep executable: " + command);
            }

            if (!process.waitForProcessToFinish(600000))
            {
                process.kill();
                throw std::runtime_error("Acestep timed out");
            }

            const auto outputText = process.readAllProcessOutput().toStdString();
            if (process.getExitCode() != 0 || !std::filesystem::exists(outputAudioPath))
            {
                throw std::runtime_error(outputText.empty() ? "Acestep generation failed" : outputText);
            }
#else
            throw std::runtime_error("Acestep executable generation requires JUCE runtime support");
#endif
        }

        std::scoped_lock lock(jobsMutex_);
        if (auto it = jobs_.find(jobId); it != jobs_.end())
        {
            it->second.status.status = "completed";
            it->second.status.progress = 1.0;
            it->second.status.message = "completed";
            it->second.result.status = "completed";
            it->second.result.outputAudioPath = outputAudioPath;
        }
        logger_.info("Completed music generation job " + jobId + " model=" + selectedModel);
    }
    catch (const std::exception& exception)
    {
        logger_.error("Music generation failed for " + jobId + ": " + exception.what());
        completeFailure(exception.what());
    }
}

ModelsResponse LocalJobClient::detectModels() const
{
    ModelsResponse result;
    result.stems.push_back(envPresent("MOON_DEMUCS_EXECUTABLE") ? "demucs" : "demucs_stub");
    const bool hasAceStepApi = !trimCopy(envValue("MOON_ACE_STEP_API_URL")).empty()
#if defined(_WIN32)
        && httpGet(trimCopy(envValue("MOON_ACE_STEP_API_URL")), "/health").has_value()
#else
        && false
#endif
        ;
    const auto executablePath = trimCopy(envValue("MOON_ACE_STEP_EXECUTABLE"));
    const auto checkpointPath = trimCopy(envValue("MOON_ACE_STEP_CHECKPOINT_PATH"));
    const bool hasAceStepExecutable = !executablePath.empty();

    if (hasAceStepApi)
    {
        result.rewrite.push_back("ace_step_api");
        result.addLayer.push_back("ace_step_api");
        result.musicGeneration.push_back("ace_step_api");
    }
    else if (hasAceStepExecutable)
    {
        result.rewrite.push_back("ace_step");
        result.addLayer.push_back("ace_step");
        result.musicGeneration.push_back("ace_step");
    }
    else
    {
        result.rewrite.push_back("ace_step_stub");
        result.addLayer.push_back("ace_step_stub");
    }

    return result;
}
}
