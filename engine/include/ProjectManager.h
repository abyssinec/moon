#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "Logger.h"
#include "ProjectState.h"
#include "SessionSerializer.h"
#include "TracktionSyncBridge.h"

namespace moon::engine
{
class ProjectManager
{
public:
    explicit ProjectManager(Logger& logger);

    bool createProject(const std::string& name, const std::string& rootPath);
    bool openProject(const std::string& projectFilePath);
    bool restoreFromAutosave(const std::string& projectRootPath);
    bool saveProject();
    bool autosaveProject();
    void closeProject();
    void syncTracktionEditState(const std::string& operation);
    void syncTracktionTransportState(const std::string& operation);
    void syncTracktionRuntimeState(const std::string& syncState, const std::string& reason);

    ProjectState& state() noexcept { return state_; }
    const ProjectState& state() const noexcept { return state_; }
    std::optional<std::string> projectFilePath() const;
    std::filesystem::path projectRoot() const noexcept { return projectRoot_; }
    std::filesystem::path cacheDirectory() const noexcept { return cacheDirectory_; }
    std::filesystem::path generatedDirectory() const noexcept { return generatedDirectory_; }
    std::filesystem::path autosaveFilePath() const noexcept { return projectRoot_ / "autosave.project.json"; }
    bool hasAutosave() const;
    const TracktionSyncBridge& tracktionSyncBridge() const noexcept { return tracktionSyncBridge_; }

private:
    Logger& logger_;
    ProjectState state_;
    SessionSerializer serializer_;
    TracktionSyncBridge tracktionSyncBridge_;
    std::filesystem::path projectRoot_;
    std::filesystem::path projectFile_;
    std::filesystem::path cacheDirectory_;
    std::filesystem::path generatedDirectory_;
};
}
