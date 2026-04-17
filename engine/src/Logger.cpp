#include "Logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace moon::engine
{
namespace
{
std::string makeTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%H:%M:%S");
    return stream.str();
}
}

Logger::Logger()
    : logFilePath_(std::filesystem::path("logs") / "app.log")
{
}

void Logger::info(const std::string& message)
{
    append("INFO", message);
}

void Logger::warning(const std::string& message)
{
    append("WARN", message);
}

void Logger::error(const std::string& message)
{
    append("ERROR", message);
}

std::vector<std::string> Logger::recent() const
{
    std::scoped_lock lock(mutex_);
    return lines_;
}

std::size_t Logger::lineCount() const
{
    std::scoped_lock lock(mutex_);
    return lines_.size();
}

std::optional<std::string> Logger::latestErrorSince(std::size_t lineIndex) const
{
    std::scoped_lock lock(mutex_);
    if (lineIndex >= lines_.size())
    {
        return std::nullopt;
    }

    for (std::size_t index = lines_.size(); index > lineIndex; --index)
    {
        const auto& line = lines_[index - 1];
        if (line.find("[ERROR]") != std::string::npos)
        {
            return line;
        }
    }

    return std::nullopt;
}

void Logger::append(const std::string& level, const std::string& message)
{
    std::scoped_lock lock(mutex_);
    const auto line = "[" + makeTimestamp() + "][" + level + "] " + message;
    lines_.push_back(line);
    if (lines_.size() > 200)
    {
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - 200));
    }

    std::filesystem::create_directories(logFilePath_.parent_path());
    std::ofstream out(logFilePath_, std::ios::app);
    if (out)
    {
        out << line << "\n";
    }
}
}
