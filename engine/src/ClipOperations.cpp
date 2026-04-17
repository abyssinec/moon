#include "ClipOperations.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace moon::engine
{
namespace
{
constexpr double kMinimumClipDurationSec = 0.1;

double clampFade(double fadeSec, double clipDurationSec)
{
    return std::clamp(fadeSec, 0.0, std::max(0.0, clipDurationSec / 2.0));
}

double clipEndSec(const ClipInfo& clip)
{
    return clip.startSec + clip.durationSec;
}
}

ClipOperations::ClipOperations(Logger& logger)
    : logger_(logger)
{
}

bool ClipOperations::duplicateSelected(ProjectState& state)
{
    syncCounter(state);
    for (const auto& clip : state.clips)
    {
        if (clip.selected)
        {
            auto copy = clip;
            copy.id = "clip-" + std::to_string(nextClipId_++);
            copy.startSec += clip.durationSec;
            copy.selected = false;
            state.clips.push_back(copy);
            logger_.info("Duplicated clip " + clip.id);
            return true;
        }
    }
    return false;
}

bool ClipOperations::deleteSelected(ProjectState& state)
{
    const auto before = state.clips.size();
    state.clips.erase(
        std::remove_if(
            state.clips.begin(),
            state.clips.end(),
            [](const ClipInfo& clip)
            {
                return clip.selected;
            }),
        state.clips.end());

    if (state.clips.size() == before)
    {
        return false;
    }

    state.uiState.selectedClipId.clear();
    logger_.info("Deleted selected clip");
    return true;
}

bool ClipOperations::splitSelected(ProjectState& state, double splitTimeSec)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    syncCounter(state);
    const auto clipStart = clip->startSec;
    const auto clipEnd = clip->startSec + clip->durationSec;
    if (splitTimeSec <= clipStart + kMinimumClipDurationSec || splitTimeSec >= clipEnd - kMinimumClipDurationSec)
    {
        return false;
    }

    const auto leftDuration = splitTimeSec - clipStart;
    const auto rightDuration = clipEnd - splitTimeSec;

    ClipInfo rightClip = *clip;
    rightClip.id = "clip-" + std::to_string(nextClipId_++);
    rightClip.startSec = splitTimeSec;
    rightClip.offsetSec += leftDuration;
    rightClip.durationSec = rightDuration;
    rightClip.fadeInSec = clampFade(rightClip.fadeInSec, rightClip.durationSec);
    rightClip.fadeOutSec = clampFade(rightClip.fadeOutSec, rightClip.durationSec);
    rightClip.selected = true;

    clip->durationSec = leftDuration;
    clip->fadeInSec = clampFade(clip->fadeInSec, clip->durationSec);
    clip->fadeOutSec = clampFade(clip->fadeOutSec, clip->durationSec);
    clip->selected = false;
    state.uiState.selectedClipId = rightClip.id;
    state.clips.push_back(rightClip);
    logger_.info("Split clip " + clip->id + " at " + std::to_string(splitTimeSec) + " sec");
    return true;
}

bool ClipOperations::setSelectedGain(ProjectState& state, double gain)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    const auto clampedGain = std::clamp(gain, 0.0, 2.0);
    if (std::abs(clip->gain - clampedGain) < 0.0001)
    {
        return false;
    }

    clip->gain = clampedGain;
    logger_.info("Updated clip gain for " + clip->id + " to " + std::to_string(clampedGain));
    return true;
}

bool ClipOperations::setSelectedFadeIn(ProjectState& state, double fadeSec)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    const auto clampedFade = clampFade(fadeSec, clip->durationSec);
    if (std::abs(clip->fadeInSec - clampedFade) < 0.0001)
    {
        return false;
    }

    clip->fadeInSec = clampedFade;
    clip->fadeOutSec = std::min(clip->fadeOutSec, clampFade(clip->fadeOutSec, clip->durationSec));
    logger_.info("Updated fade-in for " + clip->id + " to " + std::to_string(clampedFade) + " sec");
    return true;
}

bool ClipOperations::setSelectedFadeOut(ProjectState& state, double fadeSec)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    const auto clampedFade = clampFade(fadeSec, clip->durationSec);
    if (std::abs(clip->fadeOutSec - clampedFade) < 0.0001)
    {
        return false;
    }

    clip->fadeOutSec = clampedFade;
    clip->fadeInSec = std::min(clip->fadeInSec, clampFade(clip->fadeInSec, clip->durationSec));
    logger_.info("Updated fade-out for " + clip->id + " to " + std::to_string(clampedFade) + " sec");
    return true;
}

bool ClipOperations::activateSelectedTake(ProjectState& state)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr || clip->takeGroupId.empty())
    {
        return false;
    }

    bool changed = false;
    for (auto& candidate : state.clips)
    {
        if (candidate.takeGroupId != clip->takeGroupId)
        {
            continue;
        }

        const auto shouldBeActive = (candidate.id == clip->id);
        if (candidate.activeTake != shouldBeActive)
        {
            candidate.activeTake = shouldBeActive;
            changed = true;
        }
    }

    if (changed)
    {
        logger_.info("Activated take " + clip->id + " for group " + clip->takeGroupId);
    }
    return changed;
}

bool ClipOperations::trimSelectedLeft(ProjectState& state, double deltaSec)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    const auto maxTrim = std::max(0.0, clip->durationSec - kMinimumClipDurationSec);
    const auto appliedDelta = std::clamp(deltaSec, -clip->offsetSec, maxTrim);
    if (std::abs(appliedDelta) < 0.0001)
    {
        return false;
    }

    clip->startSec += appliedDelta;
    clip->offsetSec += appliedDelta;
    clip->durationSec -= appliedDelta;
    clip->fadeInSec = clampFade(clip->fadeInSec, clip->durationSec);
    clip->fadeOutSec = clampFade(clip->fadeOutSec, clip->durationSec);
    logger_.info("Trimmed left edge for " + clip->id + " by " + std::to_string(appliedDelta) + " sec");
    return true;
}

bool ClipOperations::trimSelectedRight(ProjectState& state, double deltaSec)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    const auto minDelta = -(clip->durationSec - kMinimumClipDurationSec);
    const auto appliedDelta = std::max(deltaSec, minDelta);
    if (std::abs(appliedDelta) < 0.0001)
    {
        return false;
    }

    clip->durationSec = std::max(kMinimumClipDurationSec, clip->durationSec + appliedDelta);
    clip->fadeInSec = clampFade(clip->fadeInSec, clip->durationSec);
    clip->fadeOutSec = clampFade(clip->fadeOutSec, clip->durationSec);
    logger_.info("Trimmed right edge for " + clip->id + " by " + std::to_string(appliedDelta) + " sec");
    return true;
}

bool ClipOperations::createCrossfadeWithPrevious(ProjectState& state, double overlapSec)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    ClipInfo* previousClip = nullptr;
    double previousEnd = -1.0;
    for (auto& candidate : state.clips)
    {
        if (candidate.id == clip->id || candidate.trackId != clip->trackId || !candidate.activeTake)
        {
            continue;
        }

        const auto candidateEnd = clipEndSec(candidate);
        if (candidateEnd <= clip->startSec && candidateEnd > previousEnd)
        {
            previousEnd = candidateEnd;
            previousClip = &candidate;
        }
    }

    if (previousClip == nullptr)
    {
        return false;
    }

    const auto appliedOverlap = std::min(
        std::max(overlapSec, kMinimumClipDurationSec),
        std::min(previousClip->durationSec / 2.0, clip->durationSec / 2.0));
    if (appliedOverlap < kMinimumClipDurationSec)
    {
        return false;
    }

    clip->startSec = clipEndSec(*previousClip) - appliedOverlap;
    clip->fadeInSec = std::max(clip->fadeInSec, clampFade(appliedOverlap, clip->durationSec));
    previousClip->fadeOutSec = std::max(previousClip->fadeOutSec, clampFade(appliedOverlap, previousClip->durationSec));
    logger_.info(
        "Created crossfade between " + previousClip->id + " and " + clip->id
        + " with overlap " + std::to_string(appliedOverlap) + " sec");
    return true;
}

bool ClipOperations::createCrossfadeWithNext(ProjectState& state, double overlapSec)
{
    auto* clip = selectedClip(state);
    if (clip == nullptr)
    {
        return false;
    }

    ClipInfo* nextClip = nullptr;
    double nextStart = std::numeric_limits<double>::max();
    for (auto& candidate : state.clips)
    {
        if (candidate.id == clip->id || candidate.trackId != clip->trackId || !candidate.activeTake)
        {
            continue;
        }

        if (candidate.startSec >= clipEndSec(*clip) && candidate.startSec < nextStart)
        {
            nextStart = candidate.startSec;
            nextClip = &candidate;
        }
    }

    if (nextClip == nullptr)
    {
        return false;
    }

    const auto appliedOverlap = std::min(
        std::max(overlapSec, kMinimumClipDurationSec),
        std::min(nextClip->durationSec / 2.0, clip->durationSec / 2.0));
    if (appliedOverlap < kMinimumClipDurationSec)
    {
        return false;
    }

    clip->startSec = nextClip->startSec + appliedOverlap - clip->durationSec;
    clip->fadeOutSec = std::max(clip->fadeOutSec, clampFade(appliedOverlap, clip->durationSec));
    nextClip->fadeInSec = std::max(nextClip->fadeInSec, clampFade(appliedOverlap, nextClip->durationSec));
    logger_.info(
        "Created crossfade between " + clip->id + " and " + nextClip->id
        + " with overlap " + std::to_string(appliedOverlap) + " sec");
    return true;
}

ClipInfo* ClipOperations::selectedClip(ProjectState& state)
{
    for (auto& clip : state.clips)
    {
        if (clip.selected)
        {
            return &clip;
        }
    }
    return nullptr;
}

void ClipOperations::syncCounter(const ProjectState& state)
{
    for (const auto& clip : state.clips)
    {
        if (clip.id.rfind("clip-", 0) == 0)
        {
            nextClipId_ = std::max(nextClipId_, std::stoi(clip.id.substr(5)) + 1);
        }
    }
}
}
