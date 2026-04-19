#include "LocalJobClient.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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
}

LocalJobClient::LocalJobClient(std::string backendUrl, Logger& logger)
    : backendUrl_(std::move(backendUrl))
    , logger_(logger)
{
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

JobStatusResponse LocalJobClient::getJob(const std::string& jobId)
{
    auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return JobStatusResponse{jobId, "unknown", "failed", 1.0, "job not found"};
    }

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

JobResultResponse LocalJobClient::getJobResult(const std::string& jobId) const
{
    const auto it = jobs_.find(jobId);
    if (it == jobs_.end())
    {
        return JobResultResponse{jobId, "failed", {}, {}};
    }
    return it->second.result;
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

    jobs_.emplace(id, job);
    logger_.info("Created local job " + id + " for " + type + " model=" + modelName);
    return id;
}

ModelsResponse LocalJobClient::detectModels() const
{
    ModelsResponse result;
    result.stems.push_back(envPresent("MOON_DEMUCS_EXECUTABLE") ? "demucs" : "demucs_stub");
    const bool hasAceStep = envPresent("MOON_ACE_STEP_EXECUTABLE")
        || envPresent("MOON_ACE_STEP_CHECKPOINT_PATH")
        || envPresent("MOON_ACE_STEP_API_URL");
    result.rewrite.push_back(hasAceStep ? "ace_step" : "ace_step_stub");
    result.addLayer.push_back(hasAceStep ? "ace_step" : "ace_step_stub");
    return result;
}
}
