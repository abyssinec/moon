#include "TaskManager.h"

#include <algorithm>
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
    tasks_[task.id] = task;
    logger_.info("Task updated: " + task.id + " -> " + task.status);
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
        if (task.status == "queued" || task.status == "running")
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

void TaskManager::poll(ProjectState& state, TimelineFacade& timeline)
{
    for (auto& [jobId, pending] : pendingInsertions_)
    {
        auto status = client_.getJob(jobId);
        upsertTask(TaskInfo{status.id, status.type, status.status, status.progress, status.message});

        if (status.status == "failed")
        {
            logger_.error("Task failed: " + jobId + " message=" + status.message);
            continue;
        }

        if (status.status != "completed")
        {
            continue;
        }

        const auto result = client_.getJobResult(jobId);
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
        }
        else if (pending.jobType == "rewrite")
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
        }
        else if (pending.jobType == "add-layer")
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
        }
        else if (pending.jobType == "music-generation")
        {
            if (result.outputAudioPath.empty() || !std::filesystem::exists(result.outputAudioPath))
            {
                upsertTask(TaskInfo{jobId, "music-generation", "failed", 1.0, "Generated audio file not found"});
                logger_.error("Music generation output missing for " + jobId);
                continue;
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
                pending.musicRequest.selectedModel.empty() ? "ace_step" : pending.musicRequest.selectedModel,
                pending.musicRequest.stylesPrompt + (pending.musicRequest.lyricsPrompt.empty() ? "" : (" | " + pending.musicRequest.lyricsPrompt)),
                FileHash::hashPath(result.outputAudioPath));
            timeline.selectTrack(state, trackId);
            timeline.selectClip(state, clipId);
            state.uiState.playheadSec = pending.startSec;
        }

        logger_.info("Applied completed task result for " + jobId);
    }

    for (auto it = pendingInsertions_.begin(); it != pendingInsertions_.end();)
    {
        const auto taskIt = tasks_.find(it->first);
        if (taskIt != tasks_.end() && (taskIt->second.status == "completed" || taskIt->second.status == "failed"))
        {
            it = pendingInsertions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
}
