#include "WaveformService.h"
#include "WaveformDiskCache.h"

#include <algorithm>

namespace moon::engine
{
WaveformService::WaveformService(Logger& logger)
    : logger_(logger)
    , analyzer_(logger)
    , persistentCache_(std::make_unique<WaveformDiskCache>(logger))
{
}

WaveformService::~WaveformService()
{
#if MOON_HAS_JUCE
    cancelPendingUpdate();
    analysisPool_.removeAllJobs(true, 4000);
#endif
}

void WaveformService::markRequested(const std::string& path)
{
    requestWaveform(path);
}

void WaveformService::requestWaveform(const std::string& path)
{
    if (path.empty())
    {
        return;
    }

#if !MOON_HAS_JUCE
    std::scoped_lock lock(mutex_);
    auto& entry = ensureEntryLocked(path);
    if (entry.snapshot.status == Status::Ready)
    {
        return;
    }
    entry.snapshot.data = analyzer_.analyzeFile(path);
    entry.snapshot.status = entry.snapshot.data != nullptr ? Status::Ready : Status::Failed;
    entry.snapshot.revision += 1;
    return;
#else
    std::scoped_lock lock(mutex_);
    auto& entry = ensureEntryLocked(path);
    entry.lastUse = ++useCounter_;
    if (entry.snapshot.status == Status::Ready || entry.queued)
    {
        return;
    }

    entry.snapshot.status = Status::Loading;
    entry.snapshot.errorMessage.clear();
    entry.queued = true;
    analysisPool_.addJob(new AnalysisJob(*this, path), true);
#endif
}

bool WaveformService::hasWaveform(const std::string& path) const
{
    return static_cast<bool>(tryGetWaveform(path));
}

WaveformDataPtr WaveformService::tryGetWaveform(const std::string& path) const
{
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(path);
    if (it == entries_.end())
    {
        return {};
    }
    const_cast<Entry&>(it->second).lastUse = ++const_cast<WaveformService*>(this)->useCounter_;
    return it->second.snapshot.data;
}

WaveformService::Snapshot WaveformService::snapshotFor(const std::string& path) const
{
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(path);
    if (it == entries_.end())
    {
        return {};
    }
    const_cast<Entry&>(it->second).lastUse = ++const_cast<WaveformService*>(this)->useCounter_;
    return it->second.snapshot;
}

double WaveformService::durationFor(const std::string& path)
{
    requestWaveform(path);
    const auto waveform = tryGetWaveform(path);
    return waveform != nullptr ? waveform->durationSec : 0.0;
}

std::uint64_t WaveformService::revisionFor(const std::string& path) const
{
    return snapshotFor(path).revision;
}

bool WaveformService::isLoading(const std::string& path) const
{
    return snapshotFor(path).status == Status::Loading;
}

bool WaveformService::hasPendingAnalysis() const
{
    std::scoped_lock lock(mutex_);
    return pendingAnalysisCountLocked() > 0;
}

int WaveformService::pendingAnalysisCount() const
{
    std::scoped_lock lock(mutex_);
    return pendingAnalysisCountLocked();
}

void WaveformService::addListener(Listener* listener)
{
    if (listener == nullptr)
    {
        return;
    }

    std::scoped_lock lock(mutex_);
    if (std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end())
    {
        listeners_.push_back(listener);
    }
}

void WaveformService::removeListener(Listener* listener)
{
    std::scoped_lock lock(mutex_);
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
}

#if MOON_HAS_JUCE
WaveformService::AnalysisJob::AnalysisJob(WaveformService& owner, std::string sourcePath)
    : juce::ThreadPoolJob("Waveform analysis: " + sourcePath)
    , owner_(owner)
    , sourcePath_(std::move(sourcePath))
{
}

juce::ThreadPoolJob::JobStatus WaveformService::AnalysisJob::runJob()
{
    WaveformDataPtr data;
    std::string errorMessage;
    if (owner_.persistentCache_ != nullptr)
    {
        if (const auto cached = owner_.persistentCache_->load(sourcePath_))
        {
            data = std::make_shared<WaveformData>(*cached);
        }
    }
    if (data == nullptr)
    {
        data = owner_.analyzer_.analyzeFile(sourcePath_);
        if (data != nullptr && !data->mipLevels.empty())
        {
            if (owner_.persistentCache_ != nullptr)
            {
                owner_.persistentCache_->store(sourcePath_, *data);
            }
        }
    }

    if (data == nullptr || data->mipLevels.empty())
    {
        errorMessage = "Unable to analyze waveform source";
        data = std::make_shared<WaveformData>();
    }

    owner_.completeAnalysis(sourcePath_, std::move(data), std::move(errorMessage));
    return jobHasFinished;
}
#endif

void WaveformService::completeAnalysis(const std::string& path, WaveformDataPtr data, std::string errorMessage)
{
    {
        std::scoped_lock lock(mutex_);
        auto& entry = ensureEntryLocked(path);
        entry.queued = false;
        residentBytes_ -= entry.approxBytes;
        entry.snapshot.data = std::move(data);
        entry.snapshot.errorMessage = std::move(errorMessage);
        entry.snapshot.status = entry.snapshot.data != nullptr && !entry.snapshot.data->mipLevels.empty()
            ? Status::Ready
            : Status::Failed;
        entry.snapshot.revision += 1;
        entry.approxBytes = estimateBytes(entry.snapshot.data);
        residentBytes_ += entry.approxBytes;
        entry.lastUse = ++useCounter_;
        completedPaths_.push_back(path);
        pruneEntriesLocked();
    }

    if (errorMessage.empty())
    {
        logger_.info("WaveformService: ready " + path);
    }
    else
    {
        logger_.warning("WaveformService: failed " + path + " (" + errorMessage + ")");
    }

#if MOON_HAS_JUCE
    triggerAsyncUpdate();
#endif
}

WaveformService::Entry& WaveformService::ensureEntryLocked(const std::string& path)
{
    return entries_.try_emplace(path).first->second;
}

int WaveformService::pendingAnalysisCountLocked() const
{
    return static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [](const auto& entry)
    {
        return entry.second.queued || entry.second.snapshot.status == Status::Loading;
    }));
}

std::uint64_t WaveformService::estimateBytes(const WaveformDataPtr& data) const noexcept
{
    if (data == nullptr)
    {
        return 0;
    }

    std::uint64_t bytes = sizeof(WaveformData);
    for (const auto& level : data->mipLevels)
    {
        bytes += sizeof(WaveformMipLevel);
        bytes += static_cast<std::uint64_t>(level.buckets.size()) * sizeof(WaveformBucket);
    }
    return bytes;
}

void WaveformService::pruneEntriesLocked()
{
    constexpr std::uint64_t kMaxResidentBytes = 256ull * 1024ull * 1024ull;
    constexpr std::size_t kMaxResidentSources = 96;

    while ((residentBytes_ > kMaxResidentBytes || entries_.size() > kMaxResidentSources))
    {
        auto pruneIt = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
        {
            if (it->second.queued || it->second.snapshot.status == Status::Loading)
            {
                continue;
            }

            if (pruneIt == entries_.end() || it->second.lastUse < pruneIt->second.lastUse)
            {
                pruneIt = it;
            }
        }

        if (pruneIt == entries_.end())
        {
            break;
        }

        residentBytes_ -= pruneIt->second.approxBytes;
        entries_.erase(pruneIt);
    }
}

#if MOON_HAS_JUCE
void WaveformService::handleAsyncUpdate()
{
    std::vector<std::string> updatedPaths;
    std::vector<Listener*> listeners;
    {
        std::scoped_lock lock(mutex_);
        updatedPaths.swap(completedPaths_);
        listeners = listeners_;
    }

    for (const auto& path : updatedPaths)
    {
        for (auto* listener : listeners)
        {
            if (listener != nullptr)
            {
                listener->waveformSourceUpdated(path);
            }
        }
    }
}
#endif
}
