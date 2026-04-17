#include "SettingsService.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace moon::engine
{
namespace
{
std::string escape(const std::string& text)
{
    std::string output;
    output.reserve(text.size());
    for (const auto ch : text)
    {
        if (ch == '\\')
        {
            output += "\\\\";
        }
        else if (ch == '\"')
        {
            output += "\\\"";
        }
        else
        {
            output += ch;
        }
    }
    return output;
}

std::string extractString(const std::string& content, const std::string& key, const std::string& fallback)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return match[1].str();
    }
    return fallback;
}

int extractInt(const std::string& content, const std::string& key, int fallback)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return std::stoi(match[1].str());
    }
    return fallback;
}
}

SettingsService::SettingsService(std::filesystem::path settingsPath)
    : settingsPath_(std::move(settingsPath))
{
}

Settings SettingsService::load() const
{
    std::ifstream in(settingsPath_);
    if (!in)
    {
        return {};
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto content = buffer.str();

    Settings settings;
    settings.backendUrl = extractString(content, "backend_url", settings.backendUrl);
    settings.cacheDirectory = extractString(content, "cache_directory", settings.cacheDirectory.string());
    settings.defaultSampleRate = extractInt(content, "default_sample_rate", settings.defaultSampleRate);
    return settings;
}

bool SettingsService::save(const Settings& settings) const
{
    std::filesystem::create_directories(settingsPath_.parent_path());

    std::ofstream out(settingsPath_, std::ios::trunc);
    if (!out)
    {
        return false;
    }

    out << "{\n";
    out << "  \"backend_url\": \"" << escape(settings.backendUrl) << "\",\n";
    out << "  \"cache_directory\": \"" << escape(settings.cacheDirectory.string()) << "\",\n";
    out << "  \"default_sample_rate\": " << settings.defaultSampleRate << "\n";
    out << "}\n";
    return true;
}
}
