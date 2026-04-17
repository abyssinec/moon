#include "TracktionSyncBridge.h"

namespace moon::engine
{
TracktionSyncBridge::TracktionSyncBridge(Logger& logger)
    : logger_(logger)
{
}

bool TracktionSyncBridge::runtimeAvailable() const noexcept
{
#if MOON_HAS_TRACKTION
    return true;
#else
    return false;
#endif
}

std::string TracktionSyncBridge::runtimeSummary() const
{
#if MOON_HAS_TRACKTION
    return "tracktion-compiled seam";
#else
    return "tracktion unavailable -> lightweight fallback";
#endif
}

void TracktionSyncBridge::markProjectOpened(ProjectState& state)
{
    updateState(state, runtimeAvailable() ? "ready" : "fallback", runtimeSummary());
}

void TracktionSyncBridge::markProjectCreated(ProjectState& state)
{
    updateState(state, runtimeAvailable() ? "ready" : "fallback", runtimeSummary());
}

void TracktionSyncBridge::syncEditState(ProjectState& state, const std::string& operation)
{
    if (runtimeAvailable())
    {
        updateState(state, "deferred-sync", "tracktion edit seam prepared for " + operation);
        logger_.info("Tracktion edit sync seam marked operation " + operation);
        return;
    }

    updateState(state, "fallback", "lightweight edit path used for " + operation);
}

void TracktionSyncBridge::syncTransportState(ProjectState& state, const std::string& operation)
{
    if (runtimeAvailable())
    {
        updateState(state, "deferred-sync", "tracktion transport seam prepared for " + operation);
        logger_.info("Tracktion transport sync seam marked operation " + operation);
        return;
    }

    updateState(state, "fallback", "lightweight transport path used for " + operation);
}

void TracktionSyncBridge::syncRuntimeState(ProjectState& state, const std::string& syncState, const std::string& reason)
{
    if (runtimeAvailable())
    {
        updateState(state, syncState.empty() ? "deferred-sync" : syncState, "tracktion runtime seam active for " + reason);
        logger_.info("Tracktion runtime sync state updated: " + reason);
        return;
    }

    updateState(state, syncState.empty() ? "fallback" : syncState, "lightweight runtime fallback for " + reason);
}

void TracktionSyncBridge::updateState(ProjectState& state, const std::string& syncState, const std::string& reason) const
{
    state.engineState.tracktionRuntimeReady = runtimeAvailable();
    state.engineState.tracktionSyncState = syncState;
    state.engineState.tracktionSyncReason = reason;
}
}
