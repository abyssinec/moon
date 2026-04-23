#include "AppController.h"
#include "MainWindow.h"

#if MOON_HAS_JUCE
#include <memory>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        const auto executableFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const auto executablePath = std::filesystem::path(executableFile.getFullPathName().toStdString());
        const auto executableDir = executablePath.parent_path();

        std::error_code ec;
        std::filesystem::current_path(executableDir, ec);

        if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
        {
            const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::app);
            if (out)
            {
                out << "[bootstrap] application initialise begin\n";
                out << "[bootstrap] executable=" << executablePath.string() << "\n";
                out << "[bootstrap] executable_dir=" << executableDir.string() << "\n";
                out << "[bootstrap] cwd_after_set=" << std::filesystem::current_path().string() << "\n";
                if (ec)
                    out << "[bootstrap] cwd_set_error=" << ec.message() << "\n";
            }
        }
        controller_ = std::make_unique<AppController>();
        if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
        {
            const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
            std::ofstream out(path, std::ios::app);
            if (out) out << "[bootstrap] controller allocated\n";
        }
        controller_->startup();
        if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
        {
            const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
            std::ofstream out(path, std::ios::app);
            if (out) out << "[bootstrap] controller startup returned\n";
        }
        window_ = std::make_unique<MainWindow>(*controller_);
        if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
        {
            const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
            std::ofstream out(path, std::ios::app);
            if (out) out << "[bootstrap] main window allocated\n";
        }
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
