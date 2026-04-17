#pragma once

#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

#include "Logger.h"

namespace moon::engine
{
struct HealthResponse
{
    std::string status{"unknown"};
    std::string backend{"offline"};
};

struct ModelsResponse
{
    std::vector<std::string> stems;
    std::vector<std::string> rewrite;
    std::vector<std::string> addLayer;
};

struct JobStatusResponse
{
    std::string id;
    std::string type;
    std::string status{"queued"};
    double progress{0.0};
    std::string message{"queued"};
};

struct JobResultResponse
{
    std::string id;
    std::string status{"queued"};
    std::unordered_map<std::string, std::string> outputs;
    std::string outputAudioPath;
};

class AIJobClient
{
public:
    AIJobClient(std::string backendUrl, Logger& logger);

    HealthResponse healthCheck() const;
    ModelsResponse models() const;
    std::string createStemsJob(const std::string& inputAudioPath, const std::string& modelName);
    std::string createRewriteJob(const std::string& inputAudioPath,
                                 const std::string& prompt,
                                 const std::string& modelName,
                                 double durationSec);
    std::string createAddLayerJob(const std::string& inputAudioPath,
                                  const std::string& prompt,
                                  const std::string& modelName,
                                  double durationSec);
    JobStatusResponse getJob(const std::string& jobId);
    JobResultResponse getJobResult(const std::string& jobId) const;
    bool backendReachable() const noexcept { return backendReachable_; }
    const std::string& backendUrl() const noexcept { return backendUrl_; }

private:
    struct LocalJob
    {
        JobStatusResponse status;
        JobResultResponse result;
    };

    std::optional<std::string> httpGet(const std::string& path) const;
    std::optional<std::string> httpPost(const std::string& path, const std::string& jsonBody) const;
    static std::optional<std::string> extractJsonString(const std::string& json, const std::string& key);
    static std::optional<double> extractJsonDouble(const std::string& json, const std::string& key);
    static std::unordered_map<std::string, std::string> extractJsonObjectStrings(const std::string& json, const std::string& key);

    std::string createJob(const std::string& type,
                          const std::string& inputAudioPath,
                          const std::string& modelName,
                          const std::string& prompt,
                          double durationSec);
    void markBackendReachable() const noexcept;
    void markBackendFallback() const noexcept;

    std::string backendUrl_;
    Logger& logger_;
    std::unordered_map<std::string, LocalJob> jobs_;
    int nextJobId_{1};
    mutable bool backendReachable_{false};
};
}
