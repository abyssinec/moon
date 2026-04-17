#pragma once

#include "Logger.h"
#include "ProjectState.h"

namespace moon::engine
{
class ClipOperations
{
public:
    explicit ClipOperations(Logger& logger);
    bool duplicateSelected(ProjectState& state);
    bool deleteSelected(ProjectState& state);
    bool splitSelected(ProjectState& state, double splitTimeSec);
    bool setSelectedGain(ProjectState& state, double gain);
    bool setSelectedFadeIn(ProjectState& state, double fadeSec);
    bool setSelectedFadeOut(ProjectState& state, double fadeSec);
    bool activateSelectedTake(ProjectState& state);
    bool trimSelectedLeft(ProjectState& state, double deltaSec);
    bool trimSelectedRight(ProjectState& state, double deltaSec);
    bool createCrossfadeWithPrevious(ProjectState& state, double overlapSec);
    bool createCrossfadeWithNext(ProjectState& state, double overlapSec);

private:
    ClipInfo* selectedClip(ProjectState& state);
    void syncCounter(const ProjectState& state);

    Logger& logger_;
    int nextClipId_{1};
};
}
