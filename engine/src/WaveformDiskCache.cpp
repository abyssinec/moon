#include "WaveformDiskCache.h"

#include <array>
#include <cstdlib>
#include <fstream>

namespace moon::engine
{
namespace
{
constexpr std::uint32_t kWaveformCacheMagic = 0x4d4f4f4e;
constexpr std::uint32_t kWaveformCacheVersion = 3;

struct SourceValidationInfo
{
    std::uint64_t fileSize{0};
    std::int64_t modifiedCount{0};
    std::uint64_t partialHash{0};
};

std::filesystem::path resolveWaveformCacheRoot()
{
#if defined(_WIN32)
    if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
    {
        return std::filesystem::path(localAppData) / "MoonAudioEditor" / "waveforms";
    }
#endif
    return std::filesystem::path("cache") / "waveforms";
}

template <typename T>
bool readValue(std::ifstream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

template <typename T>
void writeValue(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

std::uint64_t fnv1a64(const std::uint8_t* bytes, std::size_t size, std::uint64_t seed = 1469598103934665603ull)
{
    auto hash = seed;
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

SourceValidationInfo readSourceValidationInfo(const std::string& sourcePath)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    SourceValidationInfo info;
    info.fileSize = fs::file_size(sourcePath, ec);
    const auto modified = fs::last_write_time(sourcePath, ec);
    info.modifiedCount = static_cast<std::int64_t>(modified.time_since_epoch().count());
    return info;
}
}

WaveformDiskCache::WaveformDiskCache(Logger& logger)
    : logger_(logger)
    , cacheRoot_(resolveWaveformCacheRoot())
{
}

std::optional<WaveformData> WaveformDiskCache::load(const std::string& sourcePath) const
{
    const auto cachePath = cachePathFor(sourcePath);
    if (!std::filesystem::exists(cachePath))
    {
        return std::nullopt;
    }

    std::ifstream in(cachePath, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!readValue(in, magic) || !readValue(in, version) || magic != kWaveformCacheMagic || version != kWaveformCacheVersion)
    {
        return std::nullopt;
    }

    WaveformData data;
    SourceValidationInfo storedValidation;
    std::uint32_t levelCount = 0;
    if (!readValue(in, data.sampleRate) ||
        !readValue(in, data.channelCount) ||
        !readValue(in, data.totalSamples) ||
        !readValue(in, data.durationSec) ||
        !readValue(in, storedValidation.fileSize) ||
        !readValue(in, storedValidation.modifiedCount) ||
        !readValue(in, storedValidation.partialHash) ||
        !readValue(in, levelCount))
    {
        return std::nullopt;
    }

    auto currentValidation = readSourceValidationInfo(sourcePath);
    currentValidation.partialHash = std::strtoull(computePartialContentHash(sourcePath).c_str(), nullptr, 10);
    if (storedValidation.fileSize != currentValidation.fileSize ||
        storedValidation.modifiedCount != currentValidation.modifiedCount ||
        storedValidation.partialHash != currentValidation.partialHash)
    {
        logger_.warning("WaveformDiskCache: stale validation for " + sourcePath);
        return std::nullopt;
    }

    data.mipLevels.reserve(levelCount);
    for (std::uint32_t levelIndex = 0; levelIndex < levelCount; ++levelIndex)
    {
        WaveformMipLevel level;
        std::uint64_t bucketCount = 0;
        if (!readValue(in, level.samplesPerBucket) || !readValue(in, bucketCount))
        {
            return std::nullopt;
        }

        level.buckets.resize(static_cast<std::size_t>(bucketCount));
        if (bucketCount > 0)
        {
            in.read(reinterpret_cast<char*>(level.buckets.data()), static_cast<std::streamsize>(bucketCount * sizeof(WaveformBucket)));
            if (!in)
            {
                return std::nullopt;
            }
        }

        data.mipLevels.push_back(std::move(level));
    }

    logger_.info("WaveformDiskCache: loaded " + sourcePath);
    return data;
}

void WaveformDiskCache::store(const std::string& sourcePath, const WaveformData& data) const
{
    try
    {
        std::filesystem::create_directories(cacheRoot_);
        const auto cachePath = cachePathFor(sourcePath);
        const auto tmpPath = cachePath.string() + ".tmp";
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return;
        }

        writeValue(out, kWaveformCacheMagic);
        writeValue(out, kWaveformCacheVersion);
        writeValue(out, data.sampleRate);
        writeValue(out, data.channelCount);
        writeValue(out, data.totalSamples);
        writeValue(out, data.durationSec);
        auto validation = readSourceValidationInfo(sourcePath);
        validation.partialHash = std::strtoull(computePartialContentHash(sourcePath).c_str(), nullptr, 10);
        writeValue(out, validation.fileSize);
        writeValue(out, validation.modifiedCount);
        writeValue(out, validation.partialHash);
        const auto levelCount = static_cast<std::uint32_t>(data.mipLevels.size());
        writeValue(out, levelCount);
        for (const auto& level : data.mipLevels)
        {
            writeValue(out, level.samplesPerBucket);
            const auto bucketCount = static_cast<std::uint64_t>(level.buckets.size());
            writeValue(out, bucketCount);
            if (bucketCount > 0)
            {
                out.write(reinterpret_cast<const char*>(level.buckets.data()), static_cast<std::streamsize>(bucketCount * sizeof(WaveformBucket)));
            }
        }

        out.flush();
        out.close();
        std::error_code ec;
        std::filesystem::remove(cachePath, ec);
        std::filesystem::rename(tmpPath, cachePath);
        logger_.info("WaveformDiskCache: stored " + sourcePath);
    }
    catch (...)
    {
        logger_.warning("WaveformDiskCache: failed to store cache for " + sourcePath);
    }
}

std::string WaveformDiskCache::makeCacheKey(const std::string& sourcePath) const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto fileSize = fs::file_size(sourcePath, ec);
    const auto modified = fs::last_write_time(sourcePath, ec);
    const auto modifiedCount = modified.time_since_epoch().count();
    const auto partialHash = computePartialContentHash(sourcePath);
    const auto raw = sourcePath + "|" + std::to_string(fileSize) + "|" + std::to_string(modifiedCount) + "|" + partialHash;
    return std::to_string(std::hash<std::string>{}(raw));
}

std::string WaveformDiskCache::computePartialContentHash(const std::string& sourcePath) const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto fileSize = fs::file_size(sourcePath, ec);
    if (ec || fileSize == 0)
    {
        return "0";
    }

    std::ifstream in(sourcePath, std::ios::binary);
    if (!in)
    {
        return "0";
    }

    constexpr std::size_t kChunkSize = 64 * 1024;
    std::array<std::uint8_t, kChunkSize> buffer{};
    std::uint64_t hash = 1469598103934665603ull;

    auto readChunk = [&](std::uint64_t offset)
    {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = static_cast<std::size_t>(in.gcount());
        if (bytesRead > 0)
        {
            hash = fnv1a64(buffer.data(), bytesRead, hash);
        }
    };

    readChunk(0);
    if (fileSize > kChunkSize * 2)
    {
        readChunk((fileSize / 2) - (kChunkSize / 2));
    }
    if (fileSize > kChunkSize)
    {
        readChunk(fileSize - kChunkSize);
    }

    return std::to_string(hash);
}

std::filesystem::path WaveformDiskCache::cachePathFor(const std::string& sourcePath) const
{
    return cacheRoot_ / (makeCacheKey(sourcePath) + ".mwf");
}
}
