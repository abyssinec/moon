#pragma once

#include <string>

#include "Logger.h"

namespace moon::engine
{
class ProjectManager;
class TimelineFacade;
class TransportFacade;

class EngineRuntimeCoordinator
{
public:
    explicit EngineRuntimeCoordinator(Logger& logger);

    void noteTimelineOperation(ProjectManager& projectManager, const std::string& operation);
    void noteTransportOperation(ProjectManager& projectManager, const std::string& operation);
    void sync(ProjectManager& projectManager,
              const TimelineFacade& timeline,
              const TransportFacade& transport,
              const std::string& reason);

private:
    Logger& logger_;
};
}
