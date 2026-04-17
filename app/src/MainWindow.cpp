#include "MainWindow.h"

#if MOON_HAS_JUCE
namespace moon::app
{
MainWindow::MainWindow(AppController& controller)
    : juce::DocumentWindow(controller.windowTitle(),
                           juce::Colours::darkslategrey,
                           juce::DocumentWindow::allButtons)
    , controller_(controller)
    , mainComponent_(std::make_unique<MainComponent>(controller))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentOwned(mainComponent_.release(), true);
    centreWithSize(1400, 900);
    setVisible(true);
    startTimerHz(4);

    if (!controller_.startupNotice().empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Startup Notice",
            controller_.startupNotice());
    }
}

void MainWindow::closeButtonPressed()
{
    if (controller_.hasUnsavedChanges())
    {
        const auto saveResult = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            "Unsaved project changes",
            "There are unsaved project changes. Quit anyway after saving project state?",
            "Quit",
            "Cancel");
        if (!saveResult)
        {
            return;
        }
    }

    if (!controller_.canCloseSafely())
    {
        const auto result = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            "Tasks are still running",
            "There are active tasks in progress. Quit anyway after saving project state?",
            "Quit",
            "Cancel");
        if (!result)
        {
            return;
        }
    }

    controller_.prepareForShutdown();
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::timerCallback()
{
    const auto desiredTitle = controller_.windowTitle();
    if (getName().toStdString() != desiredTitle)
    {
        setName(desiredTitle);
    }
}
}
#endif
