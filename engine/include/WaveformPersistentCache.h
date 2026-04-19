#pragma once

#include "WaveformData.h"

#include <optional>
#include <string>

namespace moon::engine
{
class WaveformPersistentCache
{
public:
    virtual ~WaveformPersistentCache() = default;

    virtual std::optional<WaveformData> load(const std::string& sourcePath) const = 0;
    virtual void store(const std::string& sourcePath, const WaveformData& data) const = 0;
};
}
