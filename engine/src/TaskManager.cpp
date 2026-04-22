#include "TaskManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "FileHash.h"
#include "TimelineFacade.h"

namespace moon::engine
{
namespace
{
bool trackHasAnyClip(const ProjectState& state, const std::string& trackId)
{
    return std::any_of(
        state.clips.begin(),
        state.clips.end(),
        [&trackId](const ClipInfo& clip)
        {
            return clip.trackId == trackId;
        });
}

std::string ensureGeneratedTrack(ProjectState& state, TimelineFacade& timeline, const std::string& preferredName)
{
    auto name = preferredName.empty() ? std::string("Generated Track") : preferredName;
    int suffix = 2;
    while (true)
    {
        const auto existing = std::find_if(
            state.tracks.begin(),
            state.tracks.end(),
            [&name](const TrackInfo& track)
            {
                return track.name == name;
            });
        if (existing == state.tracks.end())
        {
            return timeline.ensureTrack(state, name);
        }

        if (!trackHasAnyClip(state, existing->id))
        {
            return existing->id;
        }

        name = preferredName + " " + std::to_string(suffix++);
    }
}
}

TaskManager::TaskManager(JobClientProtocol& client, Logger& logger)
    : client_(client)
    , logger_(logger)
{
}

void TaskManager::upsertTask(const TaskInfo& task)
{
    const auto existing = tasks_.find(task.id);
    const bool isNewTask = existing == tasks_.end();
    const bool statusChanged = isNewTask || existing->second.status != task.status;
    const bool messageChanged = isNewTask || existing->second.message != task.message;
    const bool typeChanged = isNewTask || existing->second.type != task.type;
    tasks_[task.id] = task;

    if (isNewTask)
    {
        logger_.info("Task queued: " + task.id + " [" + task.type + "]");
        return;
    }

    if (statusChanged || typeChanged)
    {
        if (task.status == "failed")
        {
            logger_.error("Task failed: " + task.id + (task.message.empty() ? std::string{} : (" message=" + task.message)));
        }
        else if (task.status == "completed")
        {
            logger_.info("Task completed: " + task.id);
        }
        else if (task.status == "cancelled")
        {
            logger_.info("Task cancelled: " + task.id);
        }
        else
        {
            logger_.info("Task state changed: " + task.id + " -> " + task.status);
        }
        return;
    }

    if (task.status == "failed" && messageChanged)
    {
        logger_.error("Task failure detail updated: " + task.id + " message=" + task.message);
    }
}

std::unordered_map<std::string, TaskInfo> TaskManager::tasks() const
{
    return tasks_;
}

std::size_t TaskManager::activeTaskCount() const
{
    std::size_t count = 0;
    for (const auto& [_, task] : tasks_)
    {
        if (task.status == "queued" || task.status == "running" || task.status == "cancelling")
        {
            ++count;
        }
    }
    return count;
}

std::size_t TaskManager::completedTaskCount() const
{
    std::size_t count = 0;
    for (const auto& [_, task] : tasks_)
    {
        if (task.status == "completed")
        {
            ++count;
        }
    }
    return count;
}

std::size_t TaskManager::failedTaskCount() const
{
    std::size_t count = 0;
    for (const auto& [_, task] : tasks_)
    {
        if (task.status == "failed")
        {
            ++count;
        }
    }
    return count;
}

std::optional<TaskInfo> TaskManager::latestFailedTask() const
{
    std::optional<TaskInfo> latest;
    for (const auto& [_, task] : tasks_)
    {
        if (task.status != "failed")
        {
            continue;
        }

        if (!latest.has_value() || task.id > latest->id)
        {
            latest = task;
        }
    }
    return latest;
}

std::vector<TaskInfo> TaskManager::recentTasks() const
{
    std::vector<TaskInfo> ordered;
    ordered.reserve(tasks_.size());
    for (const auto& [_, task] : tasks_)
    {
        ordered.push_back(task);
    }

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const TaskInfo& lhs, const TaskInfo& rhs)
        {
            return lhs.id > rhs.id;
        });

    return ordered;
}

std::string TaskManager::queueStems(const std::string& sourceClipId, const std::string& inputAudioPath, double startSec)
{
    std::lock_guard<std::mutex> lock(clientMutex_);
    const auto jobId = client_.createStemsJob(inputAudioPath, "demucs");
    pendingInsertions_[jobId] = PendingInsertion{jobId, "stems", sourceClipId, startSec, 0.0, {}};
    upsertTask(TaskInfo{jobId, "stems", "queued", 0.0, "Queued stem separation"});
    return jobId;
}

std::string TaskManager::queueRewrite(const std::string& sourceClipId,
                                      const std::string& inputAudioPath,
                                      double startSec,
                                      double durationSec,
                                      const std::string& prompt)
{
    std::lock_guard<std::mutex> lock(clientMutex_);
    const auto jobId = client_.createRewriteJob(inputAudioPath, prompt, "ace_step_stub", durationSec);
    pendingInsertions_[jobId] = PendingInsertion{jobId, "rewrite", sourceClipId, startSec, durationSec, prompt};
    upsertTask(TaskInfo{jobId, "rewrite", "queued", 0.0, "Queued rewrite"});
    return jobId;
}

std::string TaskManager::queueAddLayer(const std::string& sourceClipId,
                                       const std::string& inputAudioPath,
                                       double startSec,
                                       double durationSec,
                                       const std::string& prompt)
{
    std::lock_guard<std::mutex> lock(clientMutex_);
    const auto jobId = client_.createAddLayerJob(inputAudioPath, prompt, "ace_step_stub", durationSec);
    pendingInsertions_[jobId] = PendingInsertion{jobId, "add-layer", sourceClipId, startSec, durationSec, prompt};
    upsertTask(TaskInfo{jobId, "add-layer", "queued", 0.0, "Queued generated layer"});
    return jobId;
}

std::string TaskManager::queueMusicGeneration(const MusicGenerationRequest& request,
                                              const std::string& targetTrackId,
                                              const std::string& preferredTrackName,
                                              double startSec)
{
    std::lock_guard<std::mutex> lock(clientMutex_);
    const auto jobId = client_.createMusicGenerationJob(request);
    pendingInsertions_[jobId] = PendingInsertion{
        jobId,
        "music-generation",
        {},
        std::max(0.0, startSec),
        std::max(1.0, request.durationSec),
        request.stylesPrompt,
        targetTrackId,
        preferredTrackName,
        request};
    upsertTask(TaskInfo{jobId, "music-generation", "queued", 0.0, "Queued music generation"});
    return jobId;
}

bool TaskManager::cancelTask(const std::string& jobId)
{
    std::lock_guard<std::mutex> lock(clientMutex_);
    if (!client_.cancelJob(jobId))
    {
        return false;
    }

    upsertTask(TaskInfo{jobId, "unknown", "cancelled", 1.0, "Cancelled"});
    logger_.info("Cancelled task " + jobId);
    return true;
}

void TaskManager::launchPollBatch()
{
    if (pollInFlight_ || pendingInsertions_.empty())
    {
        return;
    }

    std::vector<std::pair<std::string, PendingInsertion>> jobsToPoll;
    jobsToPoll.reserve(pendingInsertions_.size());
    for (const auto& [jobId, pending] : pendingInsertions_)
    {
        jobsToPoll.emplace_back(jobId, pending);
    }

    pollInFlight_ = true;
    pollFuture_ = std::async(
        std::launch::async,
        [this, jobsToPoll]() -> PollBatchResult
        {
            PollBatchResult batch;
            batch.jobs.reserve(jobsToPoll.size());

            for (const auto& [jobId, pending] : jobsToPoll)
            {
                PolledJob polled;
                polled.jobId = jobId;

                try
                {
                    {
                        std::lock_guard<std::mutex> lock(clientMutex_);
                        polled.status = client_.getJob(jobId);
                    }

                    if (polled.status.id.empty())
                    {
                        polled.status.id = jobId;
                    }

                    if (polled.status.type.empty())
                    {
                        polled.status.type = pending.jobType;
                    }

                    if (polled.status.status == "completed")
                    {
                        std::lock_guard<std::mutex> lock(clientMutex_);
                        polled.result = client_.getJobResult(jobId);
                    }
                }
                catch (const std::exception& ex)
                {
                    polled.status.id = jobId;
                    polled.status.type = pending.jobType;
                    polled.status.status = "failed";
                    polled.status.progress = 1.0;
                    polled.status.message = "Task polling failed";
                    polled.errorMessage = ex.what();
                }
                catch (...)
                {
                    polled.status.id = jobId;
                    polled.status.type = pending.jobType;
                    polled.status.status = "failed";
                    polled.status.progress = 1.0;
                    polled.status.message = "Task polling failed";
                    polled.errorMessage = "Unknown polling error";
                }

                batch.jobs.push_back(std::move(polled));
            }

            return batch;
        });
}

bool TaskManager::applyCompletedJobResult(const PendingInsertion& pending,
                                          const JobResultResponse& result,
                                          ProjectState& state,
                                          TimelineFacade& timeline)
{
    if (pending.jobType == "stems")
    {
        for (const auto& [stemName, path] : result.outputs)
        {
            const auto trackId = timeline.ensureTrack(state, stemName);
            timeline.insertGeneratedClip(
                state,
                trackId,
                path,
                pending.startSec,
                pending.durationSec > 0.0 ? pending.durationSec : 10.0,
                pending.sourceClipId,
                "demucs",
                stemName,
                FileHash::hashPath(path));
        }
        return true;
    }

    if (pending.jobType == "rewrite")
    {
        const auto trackId = timeline.ensureTrack(state, "Rewrite Take");
        const auto generatedClipId = timeline.insertGeneratedClip(
            state,
            trackId,
            result.outputAudioPath,
            pending.startSec,
            pending.durationSec > 0.0 ? pending.durationSec : 10.0,
            pending.sourceClipId,
            "ace_step_stub",
            pending.prompt,
            FileHash::hashPath(result.outputAudioPath));
        std::string takeGroupId = pending.sourceClipId;
        for (auto& clip : state.clips)
        {
            if (clip.id == pending.sourceClipId)
            {
                if (clip.takeGroupId.empty())
                {
                    clip.takeGroupId = pending.sourceClipId;
                }
                clip.activeTake = true;
                takeGroupId = clip.takeGroupId;
                break;
            }
        }
        for (auto& clip : state.clips)
        {
            if (clip.id == generatedClipId)
            {
                clip.fadeInSec = std::min(0.02, clip.durationSec / 2.0);
                clip.fadeOutSec = std::min(0.02, clip.durationSec / 2.0);
                clip.takeGroupId = takeGroupId;
                clip.activeTake = false;
                break;
            }
        }
        return true;
    }

    if (pending.jobType == "add-layer")
    {
        const auto trackId = timeline.ensureTrack(state, "Generated Layer");
        timeline.insertGeneratedClip(
            state,
            trackId,
            result.outputAudioPath,
            pending.startSec,
            pending.durationSec > 0.0 ? pending.durationSec : 10.0,
            pending.sourceClipId,
            "ace_step_stub",
            pending.prompt,
            FileHash::hashPath(result.outputAudioPath));
        return true;
    }

    if (pending.jobType == "music-generation")
    {
        if (result.outputAudioPath.empty() || !std::filesystem::exists(result.outputAudioPath))
        {
            upsertTask(TaskInfo{pending.jobId, "music-generation", "failed", 1.0, "Generated audio file not found"});
            logger_.error("Music generation output missing for " + pending.jobId);
            return true;
        }

        std::string trackId = pending.targetTrackId;
        const auto trackIt = std::find_if(
            state.tracks.begin(),
            state.tracks.end(),
            [&trackId](const TrackInfo& track)
            {
                return track.id == trackId;
            });
        if (trackId.empty() || trackIt == state.tracks.end() || trackHasAnyClip(state, trackId))
        {
            trackId = ensureGeneratedTrack(state, timeline, pending.preferredTrackName);
        }

        const auto clipId = timeline.insertGeneratedClip(
            state,
            trackId,
            result.outputAudioPath,
            pending.startSec,
            pending.durationSec > 0.0 ? pending.durationSec : 12.0,
            {},
            pending.musicRequest.selectedModelDisplayName.empty() ? (pending.musicRequest.selectedModel.empty() ? "ace_step" : pending.musicRequest.selectedModel) : pending.musicRequest.selectedModelDisplayName,
            pending.musicRequest.stylesPrompt + (pending.musicRequest.secondaryPrompt.empty() ? "" : (" | " + pending.musicRequest.secondaryPrompt)),
            FileHash::hashPath(result.outputAudioPath));

        for (const auto& clip : state.clips)
        {
            if (clip.id != clipId)
            {
                continue;
            }

            if (auto assetIt = state.generatedAssets.find(clip.assetId); assetIt != state.generatedAssets.end())
            {
                assetIt->second.modelId = pending.musicRequest.selectedModel;
                assetIt->second.modelName = pending.musicRequest.selectedModelDisplayName.empty()
                    ? (pending.musicRequest.selectedModel.empty() ? std::string("ace_step") : pending.musicRequest.selectedModel)
                    : pending.musicRequest.selectedModelDisplayName;
                assetIt->second.generationTarget = std::string(musicGenerationCategoryLabel(pending.musicRequest.category));
                assetIt->second.prompt = pending.musicRequest.stylesPrompt;
                assetIt->second.secondaryPrompt = pending.musicRequest.secondaryPrompt;
                assetIt->second.instrumental = pending.musicRequest.isInstrumental;
            }
            break;
        }
        timeline.selectTrack(state, trackId);
        timeline.selectClip(state, clipId);
        state.uiState.playheadSec = pending.startSec;
        return true;
    }

    return false;
}

bool TaskManager::consumePollBatch(ProjectState& state, TimelineFacade& timeline)
{
    if (!pollInFlight_ || !pollFuture_.valid())
    {
        return false;
    }

    if (pollFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        return false;
    }

    pollInFlight_ = false;
    bool changed = false;
    const auto batch = pollFuture_.get();
    for (const auto& polled : batch.jobs)
    {
        TaskInfo taskInfo{
            polled.status.id.empty() ? polled.jobId : polled.status.id,
            polled.status.type,
            polled.status.status,
            polled.status.progress,
            polled.status.message};
        if (!polled.errorMessage.empty())
        {
            taskInfo.message = polled.errorMessage;
        }

        const auto existing = tasks_.find(taskInfo.id);
        const bool taskChanged = existing == tasks_.end()
            || existing->second.status != taskInfo.status
            || std::abs(existing->second.progress - taskInfo.progress) > 0.0001
            || existing->second.message != taskInfo.message;
        upsertTask(taskInfo);
        changed = changed || taskChanged;

        if (polled.status.status == "completed" && polled.result.has_value())
        {
            if (const auto pendingIt = pendingInsertions_.find(polled.jobId); pendingIt != pendingInsertions_.end())
            {
                changed = applyCompletedJobResult(pendingIt->second, *polled.result, state, timeline) || changed;
                logger_.info("Applied completed task result for " + polled.jobId);
            }
        }
    }

    return changed;
}

bool TaskManager::poll(ProjectState& state, TimelineFacade& timeline)
{
    bool changed = consumePollBatch(state, timeline);
    if (!pollInFlight_ && !pendingInsertions_.empty())
    {
        launchPollBatch();
    }

    for (auto it = pendingInsertions_.begin(); it != pendingInsertions_.end();)
    {
        const auto taskIt = tasks_.find(it->first);
        if (taskIt != tasks_.end() && (taskIt->second.status == "completed" || taskIt->second.status == "failed" || taskIt->second.status == "cancelled"))
        {
            it = pendingInsertions_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    return changed;
}
}
