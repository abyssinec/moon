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

class JobClientProtocol
{
public:
    virtual ~JobClientProtocol() = default;

    virtual void setBackendUrl(std::string backendUrl) = 0;
    virtual HealthResponse healthCheck() const = 0;
    virtual ModelsResponse models() const = 0;
    virtual std::string createStemsJob(const std::string& inputAudioPath, const std::string& modelName) = 0;
    virtual std::string createRewriteJob(const std::string& inputAudioPath,
                                         const std::string& prompt,
                                         const std::string& modelName,
                                         double durationSec) = 0;
    virtual std::string createAddLayerJob(const std::string& inputAudioPath,
                                          const std::string& prompt,
                                          const std::string& modelName,
                                          double durationSec) = 0;
    virtual JobStatusResponse getJob(const std::string& jobId) = 0;
    virtual JobResultResponse getJobResult(const std::string& jobId) const = 0;
    virtual bool backendReachable() const noexcept = 0;
    virtual const std::string& backendUrl() const noexcept = 0;
};

class AIJobClient : public JobClientProtocol
{
public:
    AIJobClient(std::string backendUrl, Logger& logger);
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
    JobStatusResponse getJob(const std::string& jobId) override;
    JobResultResponse getJobResult(const std::string& jobId) const override;
    bool backendReachable() const noexcept override { return backendReachable_; }
    const std::string& backendUrl() const noexcept override { return backendUrl_; }

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
