#pragma once

#include "AppController.h"

#if MOON_HAS_JUCE
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

namespace moon::app
{
class MainWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    explicit MainWindow(AppController& controller);
    void closeButtonPressed() override;

private:
    void timerCallback() override;

    AppController& controller_;
    std::unique_ptr<MainComponent> mainComponent_;
    bool fullscreenApplied_{false};
};
}
#else
namespace moon::app
{
class MainWindow
{
public:
    explicit MainWindow(AppController&) {}
};
}
#endif
