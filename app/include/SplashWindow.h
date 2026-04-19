#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BootstrapCoordinator.h"

namespace moon::app
{
class SplashContent final : public juce::Component
{
public:
    SplashContent();

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setStatus(const BootstrapStatus& status);

    std::function<void()> onRetry;
    std::function<void()> onContinueFallback;
    std::function<void()> onShowDetails;

private:
    juce::Label titleLabel_;
    juce::Label headlineLabel_;
    juce::Label detailLabel_;
    juce::TextButton retryButton_{"Retry"};
    juce::TextButton continueFallbackButton_{"Continue in Fallback"};
    juce::TextButton showDetailsButton_{"Show Details"};
};

class SplashWindow final : public juce::DocumentWindow
{
public:
    SplashWindow();
    void closeButtonPressed() override;

    void setStatus(const BootstrapStatus& status);

    std::function<void()> onRetry;
    std::function<void()> onContinueFallback;
    std::function<void()> onShowDetails;

private:
    std::unique_ptr<SplashContent> content_;
};
}
