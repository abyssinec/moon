#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace moon::engine
{
class Logger
{
public:
    Logger();

    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    std::vector<std::string> recent() const;
    std::size_t lineCount() const;
    std::optional<std::string> latestErrorSince(std::size_t lineIndex) const;
    std::filesystem::path logFilePath() const noexcept { return logFilePath_; }

private:
    void append(const std::string& level, const std::string& message);

    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
    std::filesystem::path logFilePath_;
};
}
