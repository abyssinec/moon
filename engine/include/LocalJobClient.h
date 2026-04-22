#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <string>
#include <vector>

#include "AIJobClient.h"

namespace moon::engine
{
class LocalJobClient final : public JobClientProtocol
{
public:
    LocalJobClient(std::string backendUrl, Logger& logger);
    ~LocalJobClient() override;

    void setBackendUrl(std::string backendUrl) override;
    HealthResponse healthCheck() const override;
    ModelsResponse models() const override;
    std::string createStemsJob(const std::string& inputAudioPath, const std::string& modelName) override;
    std::string createRewriteJob(const std::string& inputAudioPath,
                                 const std::string& prompt,
                                 const std::string& modelName,
                                 double durationSec) override;
    std::string createAddLayerJob(const std::string& inputAudioPath,
                                  const std::string& prompt,
                                  const std::string& modelName,
                                  double durationSec) override;
    std::string createMusicGenerationJob(const MusicGenerationRequest& request) override;
    JobStatusResponse getJob(const std::string& jobId) override;
    JobResultResponse getJobResult(const std::string& jobId) const override;
    bool cancelJob(const std::string& jobId) override;
    bool backendReachable() const noexcept override { return true; }
    const std::string& backendUrl() const noexcept override { return backendUrl_; }

private:
    struct LocalJob
    {
        JobStatusResponse status;
        JobResultResponse result;
    };

    std::string createJob(const std::string& type,
                          const std::string& inputAudioPath,
                          const std::string& modelName,
                          const std::string& prompt,
                          double durationSec);
    void runMusicGenerationJob(std::string jobId, MusicGenerationRequest request, std::string outputAudioPath);
    ModelsResponse detectModels() const;

    std::string backendUrl_;
    Logger& logger_;
    mutable std::mutex jobsMutex_;
    std::unordered_map<std::string, LocalJob> jobs_;
    int nextJobId_{1};
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> shuttingDown_{false};
};
}
