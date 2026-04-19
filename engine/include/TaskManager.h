#pragma once

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "AIJobClient.h"
#include "Logger.h"
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
    void poll(ProjectState& state, TimelineFacade& timeline);
    std::size_t activeTaskCount() const;
    std::size_t completedTaskCount() const;
    std::size_t failedTaskCount() const;
    std::optional<TaskInfo> latestFailedTask() const;
    std::vector<TaskInfo> recentTasks() const;

private:
    JobClientProtocol& client_;
    Logger& logger_;
    std::unordered_map<std::string, TaskInfo> tasks_;
    std::unordered_map<std::string, PendingInsertion> pendingInsertions_;
};
}
