#pragma once

#include <string>

#include "Logger.h"
#include "ProjectState.h"

namespace moon::engine
{
class TracktionSyncBridge
{
public:
    explicit TracktionSyncBridge(Logger& logger);

    bool runtimeAvailable() const noexcept;
    std::string runtimeSummary() const;
    void markProjectOpened(ProjectState& state);
    void markProjectCreated(ProjectState& state);
    void syncEditState(ProjectState& state, const std::string& operation);
    void syncTransportState(ProjectState& state, const std::string& operation);
    void syncRuntimeState(ProjectState& state, const std::string& syncState, const std::string& reason);

private:
    void updateState(ProjectState& state, const std::string& syncState, const std::string& reason) const;

    Logger& logger_;
};
}
