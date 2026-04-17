#pragma once

#include <filesystem>
#include <string>

namespace moon::engine
{
class FileHash
{
public:
    static std::string hashPath(const std::filesystem::path& path);
};
}
