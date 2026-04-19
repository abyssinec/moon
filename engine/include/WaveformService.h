#pragma once

#include "WaveformAnalyzer.h"
#include "WaveformPersistentCache.h"

#include <atomic>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if MOON_HAS_JUCE
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#endif

namespace moon::engine
{
class WaveformService
#if MOON_HAS_JUCE
    : private juce::AsyncUpdater
#endif
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void waveformSourceUpdated(const std::string& path) = 0;
    };

    enum class Status
    {
        Idle,
        Loading,
        Ready,
        Failed
    };

    struct Snapshot
    {
        Status status{Status::Idle};
        WaveformDataPtr data;
        std::string errorMessage;
        std::uint64_t revision{0};
    };

    explicit WaveformService(Logger& logger);
    ~WaveformService();

    void markRequested(const std::string& path);
    void requestWaveform(const std::string& path);
    bool hasWaveform(const std::string& path) const;
    WaveformDataPtr tryGetWaveform(const std::string& path) const;
    Snapshot snapshotFor(const std::string& path) const;
    double durationFor(const std::string& path);
    std::uint64_t revisionFor(const std::string& path) const;
    bool isLoading(const std::string& path) const;
    bool hasPendingAnalysis() const;
    int pendingAnalysisCount() const;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    struct Entry
    {
        Snapshot snapshot;
        bool queued{false};
        std::uint64_t approxBytes{0};
        std::uint64_t lastUse{0};
    };

#if MOON_HAS_JUCE
    class AnalysisJob final : public juce::ThreadPoolJob
    {
    public:
        AnalysisJob(WaveformService& owner, std::string sourcePath);
        JobStatus runJob() override;

    private:
        WaveformService& owner_;
        std::string sourcePath_;
    };

    void handleAsyncUpdate() override;
#endif

    void completeAnalysis(const std::string& path, WaveformDataPtr data, std::string errorMessage);
    Entry& ensureEntryLocked(const std::string& path);
    int pendingAnalysisCountLocked() const;
    std::uint64_t estimateBytes(const WaveformDataPtr& data) const noexcept;
    void pruneEntriesLocked();

    Logger& logger_;
    WaveformAnalyzer analyzer_;
    std::unique_ptr<WaveformPersistentCache> persistentCache_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<Listener*> listeners_;
    std::vector<std::string> completedPaths_;
    std::uint64_t useCounter_{0};
    std::uint64_t residentBytes_{0};
#if MOON_HAS_JUCE
    juce::ThreadPool analysisPool_{2};
#endif
};
}
