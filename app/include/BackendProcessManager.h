#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <juce_core/juce_core.h>

#include "AIJobClient.h"

namespace moon::app
{
class BackendProcessManager
{
public:
    enum class StartResult
    {
        ExternalReady,
        OwnedLaunched,
        LauncherMissing,
        LaunchFailed
    };

    BackendProcessManager(std::string backendUrl, moon::engine::Logger& logger);
    ~BackendProcessManager();

    bool probeBackendReady();
    StartResult ensureBackendRunning();
    void shutdownOwnedBackend();

    bool ownsBackendProcess() const noexcept { return ownsBackendProcess_; }
    bool backendWasExternal() const noexcept { return backendWasExternal_; }
    bool ownedProcessStillRunning() const noexcept;
    std::string launcherCommand() const { return launcherCommand_; }
    std::string launcherPath() const { return backendRootPath_.string(); }
    std::string pythonPath() const { return pythonExecutablePath_.string(); }
    std::string backendUrl() const { return probeClient_.backendUrl(); }
    std::string lastError() const { return lastError_; }
    std::string lastProcessOutput() const { return lastProcessOutput_; }
    std::filesystem::path logFilePath() const noexcept { return logger_.logFilePath(); }
    void captureOwnedProcessOutput();
    void drainOwnedProcessOutput();
    bool lastProcessOutputSuggestsPortInUse() const;

private:
    std::optional<std::filesystem::path> locateBackendRoot() const;
    std::filesystem::path locatePythonExecutable(const std::filesystem::path& backendRoot) const;
    std::string buildLaunchCommand(const std::filesystem::path& backendRoot, const std::filesystem::path& pythonExecutable, int port) const;
    juce::StringArray buildLaunchArguments(const std::filesystem::path& backendRoot, const std::filesystem::path& pythonExecutable, int port) const;
    int selectOwnedBackendPort() const;

    moon::engine::Logger& logger_;
    moon::engine::AIJobClient probeClient_;
    std::unique_ptr<juce::ChildProcess> ownedBackendProcess_;
    std::filesystem::path backendRootPath_;
    std::filesystem::path pythonExecutablePath_;
    std::string launcherCommand_;
    std::string lastError_;
    std::string lastProcessOutput_;
    bool ownsBackendProcess_{false};
    bool backendWasExternal_{false};
};
}
