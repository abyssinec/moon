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
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        setBounds(display->userArea);
    }
    else
    {
        centreWithSize(1400, 900);
    }
    setVisible(true);
    setFullScreen(true);
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



