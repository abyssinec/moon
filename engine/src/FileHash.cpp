#include "FileHash.h"

#include <sstream>

namespace moon::engine
{
std::string FileHash::hashPath(const std::filesystem::path& path)
{
    std::ostringstream stream;
    stream << std::hex << std::hash<std::string>{}(path.string());
    return stream.str();
}
}
