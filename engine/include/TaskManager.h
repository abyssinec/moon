#pragma once

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <future>
#include <mutex>

#include "AIJobClient.h"
#include "Logger.h"
#include "MusicGeneration.h"
#include "ProjectState.h"

namespace moon::engine
{
class TimelineFacade;
}

namespace moon::engine
{
struct TaskInfo
{
    std::string id;
    std::string type;
    std::string status{"queued"};
    double progress{0.0};
    std::string message;
};

struct PendingInsertion
{
    std::string jobId;
    std::string jobType;
    std::string sourceClipId;
    double startSec{0.0};
    double durationSec{0.0};
    std::string prompt;
    std::string targetTrackId;
    std::string preferredTrackName;
    MusicGenerationRequest musicRequest;
};

class TaskManager
{
public:
    TaskManager(JobClientProtocol& client, Logger& logger);
    void upsertTask(const TaskInfo& task);
    std::unordered_map<std::string, TaskInfo> tasks() const;
    std::string queueStems(const std::string& sourceClipId, const std::string& inputAudioPath, double startSec);
    std::string queueRewrite(const std::string& sourceClipId,
                             const std::string& inputAudioPath,
                             double startSec,
                             double durationSec,
                             const std::string& prompt);
    std::string queueAddLayer(const std::string& sourceClipId,
                              const std::string& inputAudioPath,
                              double startSec,
                              double durationSec,
                              const std::string& prompt);
    std::string queueMusicGeneration(const MusicGenerationRequest& request,
                                     const std::string& targetTrackId,
                                     const std::string& preferredTrackName,
                                     double startSec);
    bool cancelTask(const std::string& jobId);
    bool poll(ProjectState& state, TimelineFacade& timeline);
    std::size_t activeTaskCount() const;
    std::size_t completedTaskCount() const;
    std::size_t failedTaskCount() const;
    std::optional<TaskInfo> latestFailedTask() const;
    std::vector<TaskInfo> recentTasks() const;

private:
    struct PolledJob
    {
        std::string jobId;
        JobStatusResponse status;
        std::optional<JobResultResponse> result;
        std::string errorMessage;
    };

    struct PollBatchResult
    {
        std::vector<PolledJob> jobs;
    };

    void launchPollBatch();
    bool consumePollBatch(ProjectState& state, TimelineFacade& timeline);
    bool applyCompletedJobResult(const PendingInsertion& pending,
                                 const JobResultResponse& result,
                                 ProjectState& state,
                                 TimelineFacade& timeline);

    JobClientProtocol& client_;
    Logger& logger_;
    std::unordered_map<std::string, TaskInfo> tasks_;
    std::unordered_map<std::string, PendingInsertion> pendingInsertions_;
    std::unordered_set<std::string> completedResultsHandled_;
    std::future<PollBatchResult> pollFuture_;
    bool pollInFlight_{false};
    mutable std::mutex clientMutex_;
};
}
