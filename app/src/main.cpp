#include "AppController.h"
#include "MainWindow.h"

#if MOON_HAS_JUCE
#include <memory>
#include <juce_events/juce_events.h>

#include "AppConfig.h"

namespace moon::app
{
class MoonApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return juce::String(AppConfig::appName.data()); }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void systemRequestedQuit() override
    {
        quit();
    }

    void initialise(const juce::String&) override
    {
        controller_ = std::make_unique<AppController>();
        controller_->startup();
        window_ = std::make_unique<MainWindow>(*controller_);
    }

    void shutdown() override
    {
        window_.reset();
        if (controller_ != nullptr)
        {
            controller_->prepareForShutdown();
        }
        controller_.reset();
    }

private:
    std::unique_ptr<AppController> controller_;
    std::unique_ptr<MainWindow> window_;
};
}

START_JUCE_APPLICATION(moon::app::MoonApplication)
#else
#include <iostream>

int main()
{
    moon::app::AppController controller;
    controller.startup();
    std::cout << "Moon Audio Editor desktop stub. Configure JUCE to enable the GUI shell.\n";
    return 0;
}
#endif
