#include "ExportService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace moon::engine
{
namespace
{
constexpr int kOutputChannels = 2;

struct PcmWavData
{
    int sampleRate{44100};
    int channelCount{2};
    std::vector<float> samples;

    std::size_t frameCount() const
    {
        return channelCount > 0 ? samples.size() / static_cast<std::size_t>(channelCount) : 0;
    }
};

const AssetInfo* findAsset(const ProjectState& state, const std::string& assetId)
{
    if (const auto sourceIt = state.sourceAssets.find(assetId); sourceIt != state.sourceAssets.end())
    {
        return &sourceIt->second;
    }
    if (const auto generatedIt = state.generatedAssets.find(assetId); generatedIt != state.generatedAssets.end())
    {
        return &generatedIt->second;
    }
    return nullptr;
}

bool anyTrackSoloed(const ProjectState& state)
{
    return std::any_of(
        state.tracks.begin(),
        state.tracks.end(),
        [](const TrackInfo& track)
        {
            return track.solo;
        });
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

const TrackInfo* findTrack(const ProjectState& state, const std::string& trackId)
{
    const auto trackIt = std::find_if(
        state.tracks.begin(),
        state.tracks.end(),
        [&trackId](const TrackInfo& track)
        {
            return track.id == trackId;
        });
    return trackIt == state.tracks.end() ? nullptr : &(*trackIt);
}

double trackGainLinear(const ProjectState& state, const std::string& trackId)
{
    if (const auto* track = findTrack(state, trackId))
    {
        return std::pow(10.0, track->gainDb / 20.0);
    }
    return 1.0;
}

float applyTrackPan(const ProjectState& state, const std::string& trackId, int channel, float sample)
{
    const auto* track = findTrack(state, trackId);
    if (track == nullptr)
    {
        return sample;
    }

    const auto pan = std::clamp(track->pan, -1.0, 1.0);
    const auto panNorm = (pan + 1.0) * 0.5;
    const auto leftGain = std::cos(panNorm * 1.5707963267948966);
    const auto rightGain = std::sin(panNorm * 1.5707963267948966);
    return sample * static_cast<float>(channel == 0 ? leftGain : rightGain);
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

bool readPcm16Wav(const std::filesystem::path& path, PcmWavData& result)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }

    char riffId[4]{};
    in.read(riffId, 4);
    if (std::string(riffId, 4) != "RIFF")
    {
        return false;
    }

    in.seekg(22, std::ios::beg);
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 44100;
    std::uint16_t bitsPerSample = 16;
    in.read(reinterpret_cast<char*>(&channels), sizeof(channels));
    in.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
    in.seekg(34, std::ios::beg);
    in.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));

    in.seekg(12, std::ios::beg);
    std::uint32_t dataSize = 0;
    while (in && !in.eof())
    {
        char chunkId[4]{};
        std::uint32_t chunkSize = 0;
        in.read(chunkId, 4);
        in.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
        if (!in)
        {
            break;
        }

        if (std::string(chunkId, 4) == "data")
        {
            dataSize = chunkSize;
            break;
        }

        in.seekg(chunkSize, std::ios::cur);
    }

    if (channels == 0 || bitsPerSample != 16 || dataSize == 0)
    {
        return false;
    }

    const auto sampleCount = dataSize / sizeof(std::int16_t);
    std::vector<std::int16_t> rawSamples(sampleCount);
    in.read(reinterpret_cast<char*>(rawSamples.data()), static_cast<std::streamsize>(dataSize));
    if (!in)
    {
        return false;
    }

    result.sampleRate = static_cast<int>(sampleRate);
    result.channelCount = static_cast<int>(channels);
    result.samples.resize(rawSamples.size());
    std::transform(
        rawSamples.begin(),
        rawSamples.end(),
        result.samples.begin(),
        [](std::int16_t sample)
        {
            return static_cast<float>(sample) / 32768.0f;
        });
    return true;
}

float sampleAt(const PcmWavData& data, double sourceFrameIndex, int outputChannel)
{
    if (data.channelCount <= 0 || data.samples.empty())
    {
        return 0.0f;
    }

    const auto totalFrames = data.frameCount();
    if (totalFrames == 0 || sourceFrameIndex < 0.0 || sourceFrameIndex >= static_cast<double>(totalFrames))
    {
        return 0.0f;
    }

    const int sourceChannel = data.channelCount == 1 ? 0 : std::min(outputChannel, data.channelCount - 1);
    const auto frame0 = static_cast<std::size_t>(std::floor(sourceFrameIndex));
    const auto frame1 = std::min(totalFrames - 1, frame0 + 1);
    const auto alpha = static_cast<float>(sourceFrameIndex - static_cast<double>(frame0));

    const auto index0 = frame0 * static_cast<std::size_t>(data.channelCount) + static_cast<std::size_t>(sourceChannel);
    const auto index1 = frame1 * static_cast<std::size_t>(data.channelCount) + static_cast<std::size_t>(sourceChannel);
    const auto sample0 = data.samples[index0];
    const auto sample1 = data.samples[index1];
    return sample0 + (sample1 - sample0) * alpha;
}

double clipEnvelope(const ClipInfo& clip, double clipLocalSec)
{
    if (clipLocalSec < 0.0 || clipLocalSec > clip.durationSec)
    {
        return 0.0;
    }

    double envelope = std::max(0.0, clip.gain);
    if (clip.fadeInSec > 0.0 && clipLocalSec < clip.fadeInSec)
    {
        const auto ratio = std::clamp(clipLocalSec / clip.fadeInSec, 0.0, 1.0);
        envelope *= std::sin(ratio * 1.5707963267948966);
    }

    if (clip.fadeOutSec > 0.0)
    {
        const auto fadeOutStart = std::max(0.0, clip.durationSec - clip.fadeOutSec);
        if (clipLocalSec > fadeOutStart)
        {
            const auto ratio = std::clamp((clip.durationSec - clipLocalSec) / clip.fadeOutSec, 0.0, 1.0);
            envelope *= std::sin(ratio * 1.5707963267948966);
        }
    }

    return envelope;
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
            pairWeight = std::cos(ratio * 1.5707963267948966);
        }
        else if (thisStart > otherStart)
        {
            pairWeight = std::sin(ratio * 1.5707963267948966);
        }

        weight = std::min(weight, pairWeight);
    }

    return std::clamp(weight, 0.0, 1.0);
}

float peakAmplitude(const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (const auto sample : samples)
    {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

void applyPeakProtection(std::vector<float>& samples)
{
    const auto peak = peakAmplitude(samples);
    if (peak <= 0.98f)
    {
        return;
    }

    const auto gain = 0.98f / peak;
    for (auto& sample : samples)
    {
        sample *= gain;
    }
}

void applySoftLimiter(std::vector<float>& samples)
{
    for (auto& sample : samples)
    {
        sample = std::tanh(sample * 1.1f) / std::tanh(1.1f);
    }
}
}

ExportService::ExportService(Logger& logger)
    : logger_(logger)
{
}

bool ExportService::exportMix(const ProjectState& state, const std::filesystem::path& outputPath)
{
    const auto durationSec = std::max(0.1, mixDuration(state));
    std::size_t renderedClipCount = 0;
    const bool ok = renderProjectRange(state, 0.0, durationSec, std::nullopt, outputPath, &renderedClipCount);
    if (ok)
    {
        logger_.info(
            "Exported mix WAV to " + outputPath.string()
            + " using " + std::to_string(renderedClipCount)
            + " audible active clips");
    }
    return ok;
}

bool ExportService::exportRegion(const ProjectState& state,
                                 double startSec,
                                 double endSec,
                                 const std::filesystem::path& outputPath)
{
    std::size_t renderedClipCount = 0;
    const bool ok = renderProjectRange(state, startSec, endSec, std::nullopt, outputPath, &renderedClipCount);
    if (ok)
    {
        logger_.info(
            "Exported selected region WAV to " + outputPath.string()
            + " using " + std::to_string(renderedClipCount)
            + " audible active clips");
    }
    return ok;
}

bool ExportService::exportStemTracks(const ProjectState& state, const std::filesystem::path& outputDirectory)
{
    static const std::vector<std::string> stemNames = {"Vocals", "Drums", "Bass", "Other", "vocals", "drums", "bass", "other"};
    std::filesystem::create_directories(outputDirectory);

    bool exportedAny = false;
    for (const auto& track : state.tracks)
    {
        if (std::find(stemNames.begin(), stemNames.end(), track.name) == stemNames.end())
        {
            continue;
        }

        const auto durationSec = std::max(0.1, trackDuration(state, track.id));
        const auto outputPath = outputDirectory / (track.name + ".wav");
        std::size_t renderedClipCount = 0;
        if (renderProjectRange(state, 0.0, durationSec, track.id, outputPath, &renderedClipCount))
        {
            logger_.info(
                "Exported stem track WAV to " + outputPath.string()
                + " using " + std::to_string(renderedClipCount)
                + " audible active clips");
            exportedAny = true;
        }
    }

    if (!exportedAny)
    {
        logger_.warning("No stem tracks available to export");
    }
    return exportedAny;
}

double ExportService::estimateMixDuration(const ProjectState& state) const
{
    return mixDuration(state);
}

bool ExportService::writePcm16Wav(const std::filesystem::path& outputPath,
                                  const std::vector<float>& interleavedSamples,
                                  int sampleRate,
                                  int channelCount) const
{
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    const auto channels = static_cast<std::uint16_t>(std::max(1, channelCount));
    constexpr std::uint16_t bitsPerSample = 16;
    const auto frameCount = static_cast<std::uint32_t>(
        std::max<std::size_t>(1, interleavedSamples.size() / static_cast<std::size_t>(channels)));
    const std::uint32_t blockAlign = channels * (bitsPerSample / 8);
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate) * blockAlign;
    const std::uint32_t dataSize = frameCount * blockAlign;
    const std::uint32_t riffSize = 36 + dataSize;

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }

    const auto writeBytes = [&out](const auto& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };

    out.write("RIFF", 4);
    writeBytes(riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);

    const std::uint32_t fmtChunkSize = 16;
    const std::uint16_t audioFormat = 1;
    writeBytes(fmtChunkSize);
    writeBytes(audioFormat);
    writeBytes(channels);
    writeBytes(static_cast<std::uint32_t>(sampleRate));
    writeBytes(byteRate);
    writeBytes(static_cast<std::uint16_t>(blockAlign));
    writeBytes(bitsPerSample);

    out.write("data", 4);
    writeBytes(dataSize);

    for (std::uint32_t index = 0; index < frameCount * channels; ++index)
    {
        const auto sample = index < interleavedSamples.size() ? interleavedSamples[static_cast<std::size_t>(index)] : 0.0f;
        const auto clamped = std::clamp(sample, -1.0f, 1.0f);
        const auto pcm = static_cast<std::int16_t>(std::lrint(clamped * 32767.0f));
        writeBytes(pcm);
    }

    return static_cast<bool>(out);
}

bool ExportService::renderProjectRange(const ProjectState& state,
                                       double startSec,
                                       double endSec,
                                       const std::optional<std::string>& trackId,
                                       const std::filesystem::path& outputPath,
                                       std::size_t* renderedClipCount) const
{
    const auto clampedStartSec = std::max(0.0, startSec);
    const auto clampedEndSec = std::max(clampedStartSec + 0.1, endSec);
    const auto durationSec = clampedEndSec - clampedStartSec;
    const auto outputFrameCount = static_cast<std::size_t>(std::ceil(durationSec * static_cast<double>(state.sampleRate)));
    std::vector<float> mixBuffer(outputFrameCount * static_cast<std::size_t>(kOutputChannels), 0.0f);
    std::unordered_map<std::string, PcmWavData> wavCache;
    std::size_t localRenderedClipCount = 0;

    for (const auto& clip : state.clips)
    {
        if (!clipParticipatesInMix(state, clip))
        {
            continue;
        }
        if (trackId.has_value() && clip.trackId != *trackId)
        {
            continue;
        }
        if (!clipIntersectsRegion(clip, clampedStartSec, clampedEndSec))
        {
            continue;
        }

        const auto* asset = findAsset(state, clip.assetId);
        if (asset == nullptr || asset->path.empty())
        {
            logger_.warning("Skipping clip " + clip.id + " during export because its asset path is missing");
            continue;
        }

        auto cacheIt = wavCache.find(asset->path);
        if (cacheIt == wavCache.end())
        {
            PcmWavData wavData;
            if (!readPcm16Wav(asset->path, wavData))
            {
                logger_.warning("Skipping unreadable WAV during export: " + asset->path);
                continue;
            }
            cacheIt = wavCache.emplace(asset->path, std::move(wavData)).first;
        }

        const auto& wavData = cacheIt->second;
        const auto overlapStartSec = std::max(clampedStartSec, clip.startSec);
        const auto overlapEndSec = std::min(clampedEndSec, clip.startSec + clip.durationSec);
        if (overlapEndSec <= overlapStartSec)
        {
            continue;
        }

        const auto outputFrameStart = static_cast<std::size_t>(
            std::max(0.0, std::floor((overlapStartSec - clampedStartSec) * static_cast<double>(state.sampleRate))));
        const auto outputFrameEnd = static_cast<std::size_t>(
            std::min<double>(outputFrameCount, std::ceil((overlapEndSec - clampedStartSec) * static_cast<double>(state.sampleRate))));
        if (outputFrameEnd <= outputFrameStart)
        {
            continue;
        }

        ++localRenderedClipCount;
        for (std::size_t frame = outputFrameStart; frame < outputFrameEnd; ++frame)
        {
            const auto timelineSec = clampedStartSec + (static_cast<double>(frame) / static_cast<double>(state.sampleRate));
            const auto clipLocalSec = timelineSec - clip.startSec;
            if (clipLocalSec < 0.0 || clipLocalSec > clip.durationSec)
            {
                continue;
            }

            const auto sourceSec = clip.offsetSec + clipLocalSec;
            const auto sourceFrame = sourceSec * static_cast<double>(wavData.sampleRate);
            const auto envelope = static_cast<float>(
                clipEnvelope(clip, clipLocalSec)
                * overlapCrossfadeWeight(state, clip, timelineSec)
                * trackGainLinear(state, clip.trackId));
            if (envelope <= 0.0f)
            {
                continue;
            }

            for (int channel = 0; channel < kOutputChannels; ++channel)
            {
                const auto sample = applyTrackPan(
                    state,
                    clip.trackId,
                    channel,
                    sampleAt(wavData, sourceFrame, channel) * envelope);
                mixBuffer[frame * static_cast<std::size_t>(kOutputChannels) + static_cast<std::size_t>(channel)] += sample;
            }
        }
    }

    if (renderedClipCount != nullptr)
    {
        *renderedClipCount = localRenderedClipCount;
    }

    applyPeakProtection(mixBuffer);
    applySoftLimiter(mixBuffer);
    return writePcm16Wav(outputPath, mixBuffer, state.sampleRate, kOutputChannels);
}


bool ExportService::clipParticipatesInMix(const ProjectState& state, const ClipInfo& clip)
{
    return clip.activeTake && trackIsAudible(state, clip.trackId);
}
bool ExportService::clipIntersectsRegion(const ClipInfo& clip, double startSec, double endSec)
{
    const auto clipStart = clip.startSec;
    const auto clipEnd = clip.startSec + clip.durationSec;
    return clipEnd > startSec && clipStart < endSec;
}

double ExportService::mixDuration(const ProjectState& state)
{
    double maxEnd = 0.0;
    for (const auto& clip : state.clips)
    {
        if (!clipParticipatesInMix(state, clip))
        {
            continue;
        }

        maxEnd = std::max(maxEnd, clip.startSec + clip.durationSec);
    }
    return maxEnd;
}

double ExportService::trackDuration(const ProjectState& state, const std::string& trackId)
{
    double maxEnd = 0.0;
    for (const auto& clip : state.clips)
    {
        if (clip.trackId != trackId || !clipParticipatesInMix(state, clip))
        {
            continue;
        }

        maxEnd = std::max(maxEnd, clip.startSec + clip.durationSec);
    }
    return maxEnd;
}
}

