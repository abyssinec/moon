#include "EngineRuntimeCoordinator.h"

#include "ProjectManager.h"
#include "TimelineFacade.h"
#include "TransportFacade.h"

namespace moon::engine
{
EngineRuntimeCoordinator::EngineRuntimeCoordinator(Logger& logger)
    : logger_(logger)
{
}

void EngineRuntimeCoordinator::noteTimelineOperation(ProjectManager& projectManager, const std::string& operation)
{
    projectManager.syncTracktionEditState(operation);
    logger_.info("Runtime coordinator noted timeline operation " + operation);
}

void EngineRuntimeCoordinator::noteTransportOperation(ProjectManager& projectManager, const std::string& operation)
{
    projectManager.syncTracktionTransportState(operation);
    logger_.info("Runtime coordinator noted transport operation " + operation);
}

void EngineRuntimeCoordinator::sync(ProjectManager& projectManager,
                                    const TimelineFacade& timeline,
                                    const TransportFacade& transport,
                                    const std::string& reason)
{
    auto transportReason = reason;
    if (transportReason.empty())
    {
        transportReason = transport.usingProjectPlayback() ? "project transport ready" : "runtime transport state updated";
    }

    projectManager.syncTracktionRuntimeState(
        transport.playbackRouteSummary(),
        transportReason + " | " + transport.projectPlaybackDiagnostic());

    auto& state = projectManager.state();
    state.engineState.timelineBackend = timeline.backendSummary();
    state.engineState.transportBackend = transport.backendSummary();
    state.engineState.tracktionRuntimeReady = state.engineState.tracktionRuntimeReady
        || timeline.tracktionBackendCompiled()
        || transport.tracktionBackendCompiled();
    if (state.engineState.tracktionSyncReason.empty())
    {
        state.engineState.tracktionSyncReason = state.engineState.tracktionRuntimeReady
            ? "tracktion seam available"
            : "tracktion seam fallback to lightweight path";
    }
}
}
