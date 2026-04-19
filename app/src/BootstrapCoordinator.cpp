#include "BootstrapCoordinator.h"

#include "AppConfig.h"

namespace moon::app
{
namespace
{
constexpr int kBackendReadyTimeoutMs = 12000;
constexpr int kBackendPollIntervalMs = 500;
}

AppBootstrapCoordinator::AppBootstrapCoordinator(AppController& controller)
    : juce::Thread("MoonBootstrapCoordinator")
    , controller_(controller)
    , processManager_(controller.currentSettings().backendUrl.empty() ? std::string(AppConfig::backendUrl) : controller.currentSettings().backendUrl, controller.logger())
{
}

AppBootstrapCoordinator::~AppBootstrapCoordinator()
{
    signalThreadShouldExit();
    stopThread(2000);
    processManager_.shutdownOwnedBackend();
}

bool AppBootstrapCoordinator::startBootstrap()
{
    publishStatus({"Loading project/session", "Preparing editor state...", false, false, false});
    if (!controller_.startup())
    {
        finishFailure("Project/session startup failed before backend bootstrap.");
        return false;
    }

    publishStatus({"Checking backend", "Looking for an existing backend instance...", false, false, false});
    startThread();
    return true;
}

void AppBootstrapCoordinator::retryBootstrap()
{
    signalThreadShouldExit();
    stopThread(2000);
    processManager_.shutdownOwnedBackend();
    startBootstrap();
}

void AppBootstrapCoordinator::continueWithFallback()
{
    signalThreadShouldExit();
    stopThread(2000);
    processManager_.shutdownOwnedBackend();
    controller_.refreshBackendStatus();
    finishReady(false);
}

std::string AppBootstrapCoordinator::detailsText() const
{
    std::string details = "Backend URL: " + processManager_.backendUrl();
    details += "\nBackend root: " + (processManager_.launcherPath().empty() ? std::string("<not resolved>") : processManager_.launcherPath());
    details += "\nPython: " + (processManager_.pythonPath().empty() ? std::string("<auto/system python>") : processManager_.pythonPath());
    details += "\nCommand: " + (processManager_.launcherCommand().empty() ? std::string("<not started>") : processManager_.launcherCommand());
    details += "\nLast error: " + (processManager_.lastError().empty() ? std::string("<none>") : processManager_.lastError());
    details += "\nLast process output: " + (processManager_.lastProcessOutput().empty() ? std::string("<none>") : processManager_.lastProcessOutput());
    details += "\nApp log: " + processManager_.logFilePath().string();
    return details;
}

void AppBootstrapCoordinator::run()
{
    const auto startResult = processManager_.ensureBackendRunning();
    if (threadShouldExit())
    {
        return;
    }

    if (startResult == BackendProcessManager::StartResult::LauncherMissing)
    {
        finishFailure("Backend launcher was not found. You can retry or continue in fallback mode.");
        return;
    }

    if (startResult == BackendProcessManager::StartResult::LaunchFailed)
    {
        finishFailure("Backend process could not be started. You can retry or continue in fallback mode.");
        return;
    }

    if (startResult == BackendProcessManager::StartResult::ExternalReady)
    {
        controller_.setBackendUrl(processManager_.backendUrl());
        publishStatus({"Opening editor", "Backend is already live. Opening main window...", false, false, false});
        finishReady(true);
        return;
    }

    publishStatus({"Launching backend", "Backend process started. Waiting for health check...", false, false, false});

    int waitedMs = 0;
    while (!threadShouldExit() && waitedMs < kBackendReadyTimeoutMs)
    {
        if (!processManager_.ownedProcessStillRunning())
        {
            processManager_.captureOwnedProcessOutput();
            if (processManager_.lastProcessOutputSuggestsPortInUse() && processManager_.probeBackendReady())
            {
                publishStatus({"Opening editor", "Backend is already bound on the target port. Opening main window...", false, false, false});
                finishReady(true);
                return;
            }

            if (processManager_.lastProcessOutputSuggestsPortInUse())
            {
                finishFailure(
                    "Backend could not start because port 8000 is already in use by another process. "
                    "Close the process using 127.0.0.1:8000, then retry, or continue in fallback mode.");
                return;
            }
            finishFailure("Backend process exited before becoming ready.");
            return;
        }

        if (processManager_.probeBackendReady())
        {
            controller_.setBackendUrl(processManager_.backendUrl());
            publishStatus({"Opening editor", "Backend is ready. Opening main window...", false, false, false});
            finishReady(true);
            return;
        }

        publishStatus({"Waiting for backend", "Still waiting for health/models response...", false, false, false});
        wait(kBackendPollIntervalMs);
        waitedMs += kBackendPollIntervalMs;
    }

    if (!threadShouldExit())
    {
        finishFailure("Backend health check timed out. Retry or continue in fallback mode.");
    }
}

void AppBootstrapCoordinator::publishStatus(const BootstrapStatus& status)
{
    juce::MessageManager::callAsync([this, status]()
    {
        if (onStatusChanged)
        {
            onStatusChanged(status);
        }
    });
}

void AppBootstrapCoordinator::finishReady(bool backendReady)
{
    juce::MessageManager::callAsync([this, backendReady]()
    {
        if (onReady)
        {
            onReady(backendReady);
        }
    });
}

void AppBootstrapCoordinator::finishFailure(const std::string& detail)
{
    publishStatus({"Backend startup failed", detail, true, true, true});
}
}
