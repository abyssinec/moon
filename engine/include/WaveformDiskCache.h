#pragma once

#include "Logger.h"
#include "WaveformData.h"
#include "WaveformPersistentCache.h"

#include <filesystem>
#include <optional>
#include <string>

namespace moon::engine
{
class WaveformDiskCache final : public WaveformPersistentCache
{
public:
    explicit WaveformDiskCache(Logger& logger);

    std::optional<WaveformData> load(const std::string& sourcePath) const override;
    void store(const std::string& sourcePath, const WaveformData& data) const override;

private:
    std::string makeCacheKey(const std::string& sourcePath) const;
    std::string computePartialContentHash(const std::string& sourcePath) const;
    std::filesystem::path cachePathFor(const std::string& sourcePath) const;

    Logger& logger_;
    std::filesystem::path cacheRoot_;
};
}
