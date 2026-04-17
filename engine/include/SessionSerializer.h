#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "ProjectState.h"

namespace moon::engine
{
class SessionSerializer
{
public:
    bool save(const ProjectState& state, const std::filesystem::path& projectFile) const;
    std::optional<ProjectState> load(const std::filesystem::path& projectFile) const;

private:
    static std::string makeRelativePath(const std::filesystem::path& projectFile, const std::string& path);
    static std::string makeAbsolutePath(const std::filesystem::path& projectFile, const std::string& path);
    static std::string escape(const std::string& text);
    static std::string extractString(const std::string& content, const std::string& key, const std::string& fallback);
    static double extractDouble(const std::string& content, const std::string& key, double fallback);
    static int extractInt(const std::string& content, const std::string& key, int fallback);
    static bool extractBool(const std::string& content, const std::string& key, bool fallback);
    static std::string extractArray(const std::string& content, const std::string& key);
};
}
