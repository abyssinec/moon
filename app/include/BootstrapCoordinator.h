#pragma once

#include <functional>
#include <memory>
#include <string>

#include <juce_events/juce_events.h>

#include "AppController.h"
#include "BackendProcessManager.h"

namespace moon::app
{
struct BootstrapStatus
{
    std::string headline;
    std::string detail;
    bool showRetry{false};
    bool showContinueFallback{false};
    bool showDetails{false};
};

class AppBootstrapCoordinator final : private juce::Thread
{
public:
    explicit AppBootstrapCoordinator(AppController& controller);
    ~AppBootstrapCoordinator() override;

    std::function<void(const BootstrapStatus&)> onStatusChanged;
    std::function<void(bool backendReady)> onReady;

    bool startBootstrap();
    void retryBootstrap();
    void continueWithFallback();
    std::string detailsText() const;

private:
    void run() override;
    void publishStatus(const BootstrapStatus& status);
    void finishReady(bool backendReady);
    void finishFailure(const std::string& detail);

    AppController& controller_;
    BackendProcessManager processManager_;
};
}
