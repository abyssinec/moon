#pragma once

#include <filesystem>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>

namespace moon::engine
{
class Logger
{
public:
    Logger();
    ~Logger();

    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    std::vector<std::string> recent() const;
    std::size_t lineCount() const;
    std::optional<std::string> latestErrorSince(std::size_t lineIndex) const;
    std::filesystem::path logFilePath() const noexcept { return logFilePath_; }

private:
    void append(const std::string& level, const std::string& message);
    void writerLoop();

    mutable std::mutex mutex_;
    std::condition_variable writerCv_;
    std::vector<std::string> lines_;
    std::deque<std::string> pendingWrites_;
    std::filesystem::path logFilePath_;
    bool stopWriter_{false};
    std::thread writerThread_;
};
}
