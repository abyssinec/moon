#include "TransportFacade.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#if MOON_HAS_JUCE
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#endif

namespace moon::engine
{
#if MOON_HAS_JUCE
namespace
{
bool anyTrackSoloed(const ProjectState& state)
{
    for (const auto& track : state.tracks)
    {
        if (track.solo)
        {
            return true;
        }
    }
    return false;
}

bool trackIsAudible(const ProjectState& state, const std::string& trackId)
{
    const auto trackIt = std::find_if(
        state.tracks.begin(),
        state.tracks.end(),
        [&trackId](const TrackInfo& track)
        {
            return track.id == trackId;
        });
    if (trackIt == state.tracks.end())
    {
        return true;
    }

    if (anyTrackSoloed(state))
    {
        return trackIt->solo;
    }
    return !trackIt->mute;
}

double clipStartSec(const ClipInfo& clip)
{
    return clip.startSec;
}

double clipEndSec(const ClipInfo& clip)
{
    return clip.startSec + clip.durationSec;
}

bool clipParticipatesInMix(const ProjectState& state, const ClipInfo& clip)
{
    return clip.activeTake && trackIsAudible(state, clip.trackId);
}

double clipEnvelope(const ClipInfo& clip, double clipLocalTime)
{
    if (clip.durationSec <= 0.0)
    {
        return 0.0;
    }

    double gain = clip.gain;
    if (clip.fadeInSec > 0.0 && clipLocalTime < clip.fadeInSec)
    {
        const auto ratio = std::clamp(clipLocalTime / clip.fadeInSec, 0.0, 1.0);
        gain *= std::sin(ratio * juce::MathConstants<double>::halfPi);
    }

    if (clip.fadeOutSec > 0.0)
    {
        const auto fadeOutStart = std::max(0.0, clip.durationSec - clip.fadeOutSec);
        if (clipLocalTime > fadeOutStart)
        {
            const auto ratio = std::clamp((clip.durationSec - clipLocalTime) / clip.fadeOutSec, 0.0, 1.0);
            gain *= std::sin(ratio * juce::MathConstants<double>::halfPi);
        }
    }

    return gain;
}

double overlapCrossfadeWeight(const ProjectState& state, const ClipInfo& clip, double timelineSec)
{
    double weight = 1.0;
    const auto thisStart = clipStartSec(clip);
    const auto thisEnd = clipEndSec(clip);

    for (const auto& other : state.clips)
    {
        if (other.id == clip.id || other.trackId != clip.trackId || !clipParticipatesInMix(state, other))
        {
            continue;
        }

        const auto otherStart = clipStartSec(other);
        const auto otherEnd = clipEndSec(other);
        const auto overlapStart = std::max(thisStart, otherStart);
        const auto overlapEnd = std::min(thisEnd, otherEnd);
        if (overlapEnd <= overlapStart || timelineSec < overlapStart || timelineSec > overlapEnd)
        {
            continue;
        }

        const auto overlapLength = overlapEnd - overlapStart;
        if (overlapLength <= 0.0)
        {
            continue;
        }

        const auto ratio = std::clamp((timelineSec - overlapStart) / overlapLength, 0.0, 1.0);
        double pairWeight = 1.0;
        if (thisStart < otherStart)
        {
            pairWeight = std::cos(ratio * juce::MathConstants<double>::halfPi);
        }
        else if (thisStart > otherStart)
        {
            pairWeight = std::sin(ratio * juce::MathConstants<double>::halfPi);
        }

        weight = std::min(weight, pairWeight);
    }

    return std::clamp(weight, 0.0, 1.0);
}

struct ProjectMixSource final : public juce::PositionableAudioSource
{
    explicit ProjectMixSource(juce::AudioFormatManager& manager)
        : formatManager(manager)
    {
    }

    void setProjectState(const ProjectState* stateToUse)
    {
        const juce::ScopedLock lock(stateLock);
        state = stateToUse;
    }

    bool canRenderLive() const
    {
        const juce::ScopedLock lock(stateLock);
        liveDiagnostic = "ok";
        if (state == nullptr)
        {
            liveDiagnostic = "no project state";
            return false;
        }

        for (const auto& clip : state->clips)
        {
            if (!clip.activeTake || !trackIsAudible(*state, clip.trackId))
            {
                continue;
            }

            const auto assetPath = resolveAssetPath(clip);
            if (assetPath.empty())
            {
                liveDiagnostic = "missing asset path for " + clip.id;
                return false;
            }

            auto* reader = getReader(assetPath);
            if (reader == nullptr || reader->sampleRate <= 0.0)
            {
                liveDiagnostic = "unreadable asset for " + clip.id;
                return false;
            }

            if (std::llround(reader->sampleRate) != static_cast<long long>(state->sampleRate))
            {
                liveDiagnostic = "sample-rate mismatch on " + clip.id;
                return false;
            }
        }

        liveDiagnostic = "ok";
        return true;
    }

    std::string diagnostic() const
    {
        const juce::ScopedLock lock(stateLock);
        return liveDiagnostic;
    }

    double timelineDurationSeconds() const
    {
        const juce::ScopedLock lock(stateLock);
        return calculateTimelineDurationSeconds();
    }

    double nominalSampleRate() const
    {
        const juce::ScopedLock lock(stateLock);
        if (state != nullptr && state->sampleRate > 0)
        {
            return static_cast<double>(state->sampleRate);
        }
        return sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRateToUse) override
    {
        juce::ignoreUnused(samplesPerBlockExpected);
        sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        const juce::ScopedLock lock(stateLock);
        if (state == nullptr || state->clips.empty())
        {
            return;
        }

        const auto requestedStart = static_cast<double>(readPosition) / sampleRate;
        const auto requestedEnd = static_cast<double>(readPosition + bufferToFill.numSamples) / sampleRate;
        const auto totalChannels = bufferToFill.buffer->getNumChannels();

        for (const auto& clip : state->clips)
        {
            if (!clip.activeTake || !trackIsAudible(*state, clip.trackId) || clip.durationSec <= 0.0)
            {
                continue;
            }

            const auto clipEnd = clip.startSec + clip.durationSec;
            if (clipEnd <= requestedStart || clip.startSec >= requestedEnd)
            {
                continue;
            }

            const auto assetPath = resolveAssetPath(clip);
            if (assetPath.empty())
            {
                continue;
            }

            auto* reader = getReader(assetPath);
            if (reader == nullptr || reader->sampleRate <= 0.0)
            {
                continue;
            }

            const auto overlapStart = std::max(requestedStart, clip.startSec);
            const auto overlapEnd = std::min(requestedEnd, clipEnd);
            const auto overlapSamples = static_cast<int>(std::ceil((overlapEnd - overlapStart) * sampleRate));
            if (overlapSamples <= 0)
            {
                continue;
            }

            const auto destinationStart = static_cast<int>(std::floor((overlapStart - requestedStart) * sampleRate));
            const auto sourceLocalTime = clip.offsetSec + std::max(0.0, overlapStart - clip.startSec);
            const auto sourceStartSample = static_cast<juce::int64>(std::floor(sourceLocalTime * reader->sampleRate));

            tempBuffer.setSize(
                std::max(1, reader->numChannels),
                overlapSamples,
                false,
                false,
                true);
            tempBuffer.clear();

            reader->read(&tempBuffer, 0, overlapSamples, sourceStartSample, true, true);

            for (int sampleIndex = 0; sampleIndex < overlapSamples; ++sampleIndex)
            {
                const auto timelineTime = overlapStart + (static_cast<double>(sampleIndex) / sampleRate);
                const auto clipLocalTime = std::max(0.0, timelineTime - clip.startSec);
                const auto envelope = static_cast<float>(
                    clipEnvelope(clip, clipLocalTime) * overlapCrossfadeWeight(*state, clip, timelineTime));
                if (envelope <= 0.0f)
                {
                    continue;
                }

                const auto destinationSample = bufferToFill.startSample + destinationStart + sampleIndex;
                if (destinationSample >= bufferToFill.buffer->getNumSamples())
                {
                    break;
                }

                for (int channel = 0; channel < totalChannels; ++channel)
                {
                    const auto sourceChannel = std::min(channel, tempBuffer.getNumChannels() - 1);
                    const auto sample = tempBuffer.getSample(sourceChannel, sampleIndex) * envelope;
                    bufferToFill.buffer->addSample(channel, destinationSample, sample);
                }
            }
        }

        readPosition += bufferToFill.numSamples;
    }

    void setNextReadPosition(juce::int64 newPosition) override
    {
        readPosition = std::max<juce::int64>(0, newPosition);
    }

    juce::int64 getNextReadPosition() const override
    {
        return readPosition;
    }

    juce::int64 getTotalLength() const override
    {
        const juce::ScopedLock lock(stateLock);
        return static_cast<juce::int64>(std::ceil(calculateTimelineDurationSeconds() * nominalSampleRateUnlocked()));
    }

    bool isLooping() const override
    {
        return false;
    }

private:
    juce::AudioFormatReader* getReader(const std::string& path) const
    {
        if (const auto it = readers.find(path); it != readers.end())
        {
            return it->second.get();
        }

        juce::File file(path);
        if (!file.existsAsFile())
        {
            return nullptr;
        }

        auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file));
        if (reader == nullptr)
        {
            return nullptr;
        }

        auto* raw = reader.get();
        readers.emplace(path, std::move(reader));
        return raw;
    }

    std::string resolveAssetPath(const ClipInfo& clip) const
    {
        if (state == nullptr)
        {
            return {};
        }

        if (const auto sourceIt = state->sourceAssets.find(clip.assetId); sourceIt != state->sourceAssets.end())
        {
            return sourceIt->second.path;
        }
        if (const auto generatedIt = state->generatedAssets.find(clip.assetId); generatedIt != state->generatedAssets.end())
        {
            return generatedIt->second.path;
        }
        return {};
    }

    double calculateTimelineDurationSeconds() const
    {
        if (state == nullptr)
        {
            return 0.0;
        }

        double maxSec = 0.0;
        for (const auto& clip : state->clips)
        {
            if (!clip.activeTake || !trackIsAudible(*state, clip.trackId))
            {
                continue;
            }
            maxSec = std::max(maxSec, clip.startSec + clip.durationSec);
        }
        return maxSec;
    }

    double nominalSampleRateUnlocked() const
    {
        if (state != nullptr && state->sampleRate > 0)
        {
            return static_cast<double>(state->sampleRate);
        }
        return sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    juce::AudioFormatManager& formatManager;
    const ProjectState* state{nullptr};
    mutable juce::CriticalSection stateLock;
    mutable std::unordered_map<std::string, std::unique_ptr<juce::AudioFormatReader>> readers;
    mutable std::string liveDiagnostic{"unknown"};
    juce::AudioBuffer<float> tempBuffer;
    double sampleRate{44100.0};
    juce::int64 readPosition{0};
};
}
#endif

struct TransportFacade::Impl
{
#if MOON_HAS_JUCE
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<ProjectMixSource> projectSource;
    juce::AudioTransportSource transportSource;

    Impl()
    {
        formatManager.registerBasicFormats();
        projectSource = std::make_unique<ProjectMixSource>(formatManager);
    }
#endif
};

TransportFacade::TransportFacade(Logger& logger)
    : logger_(logger)
    , impl_(std::make_unique<Impl>())
{
}

void TransportFacade::setPreferredBackend(TransportBackendMode mode)
{
    preferredBackendMode_ = mode;
#if MOON_HAS_TRACKTION
    activeBackendMode_ = mode;
    if (mode == TransportBackendMode::TracktionHybrid)
    {
        logger_.info("Transport backend set to tracktion-hybrid seam (runtime currently routed through JUCE/lightweight path)");
    }
    else
    {
        logger_.info("Transport backend set to lightweight");
    }
#else
    activeBackendMode_ = TransportBackendMode::Lightweight;
    if (mode == TransportBackendMode::TracktionHybrid)
    {
        logger_.warning("Tracktion transport backend requested but Tracktion is not compiled in; using lightweight backend");
    }
#endif
}

void TransportFacade::play()
{
    if (sourcePath_.empty() && !usingProjectPlayback())
    {
        logger_.error("Transport play requested with no loaded source");
        return;
    }

#if MOON_HAS_JUCE
    impl_->transportSource.start();
#endif
    playing_ = true;
    logger_.info("Transport play");
}

void TransportFacade::pause()
{
#if MOON_HAS_JUCE
    impl_->transportSource.stop();
#endif
    playing_ = false;
    logger_.info("Transport pause");
}

void TransportFacade::stop()
{
#if MOON_HAS_JUCE
    impl_->transportSource.stop();
    impl_->transportSource.setPosition(0.0);
#endif
    playing_ = false;
    playheadSec_ = 0.0;
    logger_.info("Transport stop");
}

void TransportFacade::seek(double timeSec)
{
    playheadSec_ = std::clamp(timeSec, 0.0, sourceDurationSec_ > 0.0 ? sourceDurationSec_ : timeSec);
#if MOON_HAS_JUCE
    impl_->transportSource.setPosition(playheadSec_);
#endif
    logger_.info("Transport seek " + std::to_string(playheadSec_));
}

void TransportFacade::tick(double deltaSec)
{
#if MOON_HAS_JUCE
    playheadSec_ = impl_->transportSource.getCurrentPosition();
    playing_ = impl_->transportSource.isPlaying();
    if (!playing_ && sourceDurationSec_ > 0.0 && playheadSec_ >= sourceDurationSec_)
    {
        playheadSec_ = sourceDurationSec_;
        logger_.info("Transport reached end of source");
    }
#else
    if (!playing_)
    {
        return;
    }

    playheadSec_ += deltaSec;
    if (sourceDurationSec_ > 0.0 && playheadSec_ >= sourceDurationSec_)
    {
        playheadSec_ = sourceDurationSec_;
        playing_ = false;
        logger_.info("Transport reached end of source");
    }
#endif
    (void) deltaSec;
}

void TransportFacade::loadSource(std::string sourcePath, double durationSec)
{
#if MOON_HAS_JUCE
    impl_->transportSource.stop();
    impl_->transportSource.setSource(nullptr);
    impl_->readerSource.reset();

    juce::File file{sourcePath};
    if (!file.existsAsFile())
    {
        sourcePath_.clear();
        sourceDurationSec_ = 0.0;
        playing_ = false;
        logger_.error("Transport source file does not exist: " + sourcePath);
        return;
    }

    std::unique_ptr<juce::AudioFormatReader> reader{impl_->formatManager.createReaderFor(file)};
    if (reader == nullptr)
    {
        sourcePath_.clear();
        sourceDurationSec_ = 0.0;
        playing_ = false;
        logger_.error("Unable to create JUCE reader for: " + sourcePath);
        return;
    }

    sourcePath_ = std::move(sourcePath);
    sourceDurationSec_ = durationSec;
    playheadSec_ = std::min(playheadSec_, sourceDurationSec_);
    impl_->readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    impl_->transportSource.setSource(impl_->readerSource.get(), 32768, nullptr, impl_->readerSource->sampleRate);
    impl_->transportSource.setPosition(playheadSec_);
#else
    sourcePath_ = std::move(sourcePath);
    sourceDurationSec_ = durationSec;
    playheadSec_ = std::min(playheadSec_, sourceDurationSec_);
#endif

    logger_.info("Transport loaded source " + sourcePath_);
}

void TransportFacade::clearLoadedSource()
{
#if MOON_HAS_JUCE
    impl_->transportSource.stop();
    impl_->transportSource.setSource(nullptr);
    impl_->readerSource.reset();
#endif
    sourcePath_.clear();
    sourceDurationSec_ = 0.0;
    playheadSec_ = 0.0;
    playing_ = false;
    logger_.info("Transport cleared loaded source");
}

void TransportFacade::setProjectState(const ProjectState* state)
{
#if MOON_HAS_JUCE
    impl_->projectSource->setProjectState(state);
    if (usingProjectPlayback())
    {
        sourceDurationSec_ = impl_->projectSource->timelineDurationSeconds();
    }
#endif
    (void) state;
}

void TransportFacade::useProjectPlayback(bool enabled)
{
#if MOON_HAS_JUCE
    if (enabled && usingProjectPlayback())
    {
        sourceDurationSec_ = impl_->projectSource->timelineDurationSeconds();
        return;
    }

    impl_->transportSource.stop();
    impl_->transportSource.setSource(nullptr);
    impl_->readerSource.reset();

    if (enabled)
    {
        sourcePath_ = "__project__";
        sourceDurationSec_ = impl_->projectSource->timelineDurationSeconds();
        impl_->transportSource.setSource(impl_->projectSource.get(), 32768, nullptr, impl_->projectSource->nominalSampleRate());
        impl_->transportSource.setPosition(playheadSec_);
        return;
    }

    sourcePath_.clear();
    sourceDurationSec_ = 0.0;
    playing_ = false;
#else
    (void) enabled;
#endif
}

bool TransportFacade::supportsProjectPlayback() const noexcept
{
#if MOON_HAS_JUCE
    return true;
#else
    return false;
#endif
}

bool TransportFacade::tracktionBackendCompiled() const noexcept
{
#if MOON_HAS_TRACKTION
    return true;
#else
    return false;
#endif
}

std::string TransportFacade::backendSummary() const
{
    if (activeBackendMode_ == TransportBackendMode::TracktionHybrid)
    {
#if MOON_HAS_TRACKTION
        return "tracktion-hybrid (runtime fallback)";
#else
        return "tracktion-hybrid requested -> lightweight fallback";
#endif
    }

    return "lightweight";
}

std::string TransportFacade::playbackRouteSummary() const
{
    if (usingProjectPlayback())
    {
        return "project-live";
    }
    if (hasLoadedSource())
    {
        return sourcePath_.find("timeline_preview_mix.wav") != std::string::npos
            ? "project-cached-preview"
            : "selected-source";
    }
    return "no-source";
}

bool TransportFacade::canUseProjectPlayback() const noexcept
{
#if MOON_HAS_JUCE
    return impl_->projectSource->canRenderLive();
#else
    return false;
#endif
}

std::string TransportFacade::projectPlaybackDiagnostic() const
{
#if MOON_HAS_JUCE
    return impl_->projectSource->diagnostic();
#else
    return "JUCE-disabled build";
#endif
}

bool TransportFacade::hasLoadedSource() const noexcept
{
    return !sourcePath_.empty() && sourceDurationSec_ > 0.0;
}

bool TransportFacade::usingProjectPlayback() const noexcept
{
#if MOON_HAS_JUCE
    return sourcePath_ == "__project__";
#else
    return false;
#endif
}

#if MOON_HAS_JUCE
juce::AudioSource* TransportFacade::audioSource() noexcept
{
    return &impl_->transportSource;
}
#endif
}
