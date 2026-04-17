#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Logger.h"
#include "ProjectState.h"

#if MOON_HAS_JUCE
namespace juce
{
class AudioSource;
}
#endif

namespace moon::engine
{
enum class TransportBackendMode
{
    Lightweight,
    TracktionHybrid
};

class TransportFacade
{
public:
    explicit TransportFacade(Logger& logger);

    void play();
    void pause();
    void stop();
    void seek(double timeSec);
    void tick(double deltaSec);
    void loadSource(std::string sourcePath, double durationSec);
    void clearLoadedSource();
    void setProjectState(const ProjectState* state);
    void useProjectPlayback(bool enabled);
    void setPreferredBackend(TransportBackendMode mode);
    TransportBackendMode preferredBackendMode() const noexcept { return preferredBackendMode_; }
    TransportBackendMode activeBackendMode() const noexcept { return activeBackendMode_; }
    bool tracktionBackendCompiled() const noexcept;
    std::string backendSummary() const;
    std::string playbackRouteSummary() const;
    bool supportsProjectPlayback() const noexcept;
    bool canUseProjectPlayback() const noexcept;
    std::string projectPlaybackDiagnostic() const;
    bool hasLoadedSource() const noexcept;
    bool usingProjectPlayback() const noexcept;
#if MOON_HAS_JUCE
    juce::AudioSource* audioSource() noexcept;
#endif
    double playheadSec() const noexcept { return playheadSec_; }
    bool isPlaying() const noexcept { return playing_; }
    double sourceDurationSec() const noexcept { return sourceDurationSec_; }
    const std::string& sourcePath() const noexcept { return sourcePath_; }

private:
    struct Impl;

    Logger& logger_;
    std::unique_ptr<Impl> impl_;
    double playheadSec_{0.0};
    double sourceDurationSec_{0.0};
    bool playing_{false};
    TransportBackendMode preferredBackendMode_{TransportBackendMode::Lightweight};
    TransportBackendMode activeBackendMode_{TransportBackendMode::Lightweight};
    std::string sourcePath_;
};
}
