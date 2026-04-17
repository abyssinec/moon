#include "ProjectManager.h"

#include <filesystem>

namespace moon::engine
{
ProjectManager::ProjectManager(Logger& logger)
    : logger_(logger)
    , tracktionSyncBridge_(logger)
{
}

bool ProjectManager::createProject(const std::string& name, const std::string& rootPath)
{
    projectRoot_ = std::filesystem::path(rootPath);
    cacheDirectory_ = projectRoot_ / "cache";
    generatedDirectory_ = projectRoot_ / "generated";
    projectFile_ = projectRoot_ / "project.json";

    std::filesystem::create_directories(projectRoot_);
    std::filesystem::create_directories(cacheDirectory_);
    std::filesystem::create_directories(generatedDirectory_);

    state_ = {};
    state_.projectName = name;
    tracktionSyncBridge_.markProjectCreated(state_);
    logger_.info("Created project at " + projectRoot_.string());
    return saveProject();
}

bool ProjectManager::openProject(const std::string& projectFilePath)
{
    projectFile_ = projectFilePath;
    projectRoot_ = projectFile_.parent_path();
    cacheDirectory_ = projectRoot_ / "cache";
    generatedDirectory_ = projectRoot_ / "generated";

    const auto loaded = serializer_.load(projectFile_);
    if (!loaded.has_value())
    {
        logger_.error("Failed to open project: " + projectFile_.string());
        return false;
    }

    state_ = *loaded;
    tracktionSyncBridge_.markProjectOpened(state_);
    logger_.info("Opened project " + projectFile_.string());
    return true;
}

bool ProjectManager::restoreFromAutosave(const std::string& projectRootPath)
{
    projectRoot_ = std::filesystem::path(projectRootPath);
    cacheDirectory_ = projectRoot_ / "cache";
    generatedDirectory_ = projectRoot_ / "generated";
    projectFile_ = projectRoot_ / "project.json";

    const auto autosavePath = autosaveFilePath();
    const auto loaded = serializer_.load(autosavePath);
    if (!loaded.has_value())
    {
        logger_.error("Failed to restore autosave: " + autosavePath.string());
        return false;
    }

    state_ = *loaded;
    tracktionSyncBridge_.markProjectOpened(state_);
    logger_.warning("Restored project state from autosave " + autosavePath.string());
    return true;
}

bool ProjectManager::saveProject()
{
    if (projectFile_.empty())
    {
        logger_.error("Save requested with no active project path");
        return false;
    }

    const auto saved = serializer_.save(state_, projectFile_);
    if (saved)
    {
        logger_.info("Saved project " + projectFile_.string());
    }
    else
    {
        logger_.error("Failed to save project " + projectFile_.string());
    }
    return saved;
}

bool ProjectManager::autosaveProject()
{
    if (projectRoot_.empty())
    {
        logger_.warning("Autosave requested with no active project");
        return false;
    }

    const auto autosavePath = autosaveFilePath();
    const auto saved = serializer_.save(state_, autosavePath);
    if (saved)
    {
        logger_.info("Autosaved project " + autosavePath.string());
    }
    else
    {
        logger_.error("Failed to autosave project " + autosavePath.string());
    }
    return saved;
}

void ProjectManager::closeProject()
{
    state_ = {};
    projectRoot_.clear();
    projectFile_.clear();
    cacheDirectory_.clear();
    generatedDirectory_.clear();
    logger_.info("Closed project");
}

std::optional<std::string> ProjectManager::projectFilePath() const
{
    if (projectFile_.empty())
    {
        return std::nullopt;
    }
    return projectFile_.string();
}

bool ProjectManager::hasAutosave() const
{
    return !projectRoot_.empty() && std::filesystem::exists(autosaveFilePath());
}

void ProjectManager::syncTracktionEditState(const std::string& operation)
{
    tracktionSyncBridge_.syncEditState(state_, operation);
}

void ProjectManager::syncTracktionTransportState(const std::string& operation)
{
    tracktionSyncBridge_.syncTransportState(state_, operation);
}

void ProjectManager::syncTracktionRuntimeState(const std::string& syncState, const std::string& reason)
{
    tracktionSyncBridge_.syncRuntimeState(state_, syncState, reason);
}
}
