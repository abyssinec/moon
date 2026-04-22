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
std::filesystem::path resolveLogFilePath()
{
#if defined(_WIN32)
    if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
    {
        return std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "app.log";
    }
#endif
    return std::filesystem::path("logs") / "app.log";
}

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
    : logFilePath_(resolveLogFilePath())
{
    writerThread_ = std::thread([this]() { writerLoop(); });
}

Logger::~Logger()
{
    {
        std::scoped_lock lock(mutex_);
        stopWriter_ = true;
    }
    writerCv_.notify_one();
    if (writerThread_.joinable())
    {
        writerThread_.join();
    }
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
    const auto line = "[" + makeTimestamp() + "][" + level + "] " + message;
    {
        std::scoped_lock lock(mutex_);
        lines_.push_back(line);
        if (lines_.size() > 200)
        {
            lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - 200));
        }
        pendingWrites_.push_back(line);
    }
    writerCv_.notify_one();
}

void Logger::writerLoop()
{
    while (true)
    {
        std::deque<std::string> pending;
        {
            std::unique_lock lock(mutex_);
            writerCv_.wait(lock, [this]()
            {
                return stopWriter_ || !pendingWrites_.empty();
            });

            if (stopWriter_ && pendingWrites_.empty())
            {
                break;
            }

            pending.swap(pendingWrites_);
        }

        std::filesystem::create_directories(logFilePath_.parent_path());
        std::ofstream out(logFilePath_, std::ios::app);
        if (!out)
        {
            continue;
        }

        for (const auto& line : pending)
        {
            out << line << "\n";
        }
        out.flush();
    }
}
}
