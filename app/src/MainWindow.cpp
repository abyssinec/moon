#include "MainWindow.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#if MOON_HAS_JUCE
namespace moon::app
{
namespace
{
void appendMainWindowTrace(const std::string& line)
{
    if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
    {
        const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::app);
        if (out)
        {
            out << line << "\n";
        }
    }
}
}

MainWindow::MainWindow(AppController& controller)
    : juce::DocumentWindow(controller.windowTitle(),
                           juce::Colours::darkslategrey,
                           juce::DocumentWindow::allButtons)
    , controller_(controller)
    , mainComponent_(std::make_unique<MainComponent>(controller))
{
    appendMainWindowTrace("[bootstrap] MainWindow ctor begin");
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    appendMainWindowTrace("[bootstrap] MainWindow before setContentOwned");
    setContentOwned(mainComponent_.release(), true);
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        setBounds(display->userArea);
    }
    else
    {
        centreWithSize(1400, 900);
    }
    appendMainWindowTrace("[bootstrap] MainWindow before setVisible");
    setVisible(true);
    toFront(true);
    startTimerHz(4);
    appendMainWindowTrace("[bootstrap] MainWindow ctor end");
}

void MainWindow::closeButtonPressed()
{
    controller_.prepareForShutdown();
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::timerCallback()
{
    if (!fullscreenApplied_)
    {
        fullscreenApplied_ = true;
        appendMainWindowTrace("[bootstrap] MainWindow applying deferred fullscreen");
        setFullScreen(true);
        toFront(true);
    }

    const auto desiredTitle = controller_.windowTitle();
    if (getName().toStdString() != desiredTitle)
    {
        setName(desiredTitle);
    }
}
}
#endif
