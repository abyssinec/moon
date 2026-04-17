#pragma once

#include <filesystem>
#include <string>

namespace moon::engine
{
struct Settings
{
    std::string backendUrl{"http://127.0.0.1:8000"};
    std::filesystem::path cacheDirectory{"cache"};
    int defaultSampleRate{44100};
};

class SettingsService
{
public:
    explicit SettingsService(std::filesystem::path settingsPath = std::filesystem::path("settings") / "app_settings.json");

    Settings load() const;
    bool save(const Settings& settings) const;
    const std::filesystem::path& settingsPath() const noexcept { return settingsPath_; }

private:
    std::filesystem::path settingsPath_;
};
}
