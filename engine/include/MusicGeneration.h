#pragma once

#include <array>
#include <string>
#include <string_view>

namespace moon::engine
{
enum class ModelCapability
{
    SongGeneration,
    TrackGeneration,
    TrackSeparation,
    VocalToBgm
};

inline constexpr std::string_view modelCapabilityLabel(ModelCapability capability) noexcept
{
    switch (capability)
    {
    case ModelCapability::SongGeneration:  return "song_generation";
    case ModelCapability::TrackGeneration: return "track_generation";
    case ModelCapability::TrackSeparation: return "track_separation";
    case ModelCapability::VocalToBgm:      return "vocal2bgm";
    }

    return "song_generation";
}

enum class GenerationTarget
{
    Drums,
    Bass,
    Guitar,
    Keyboard,
    Percussion,
    Strings,
    Synth,
    FX,
    Brass,
    Woodwinds,
    Vocals,
    BackingVocals,
    Song,
    Custom
};

using MusicGenerationCategory = GenerationTarget;

enum class ComputeDevicePreference
{
    Auto,
    GPU,
    CPU
};

inline constexpr std::string_view computeDevicePreferenceLabel(ComputeDevicePreference preference) noexcept
{
    switch (preference)
    {
    case ComputeDevicePreference::Auto: return "Auto";
    case ComputeDevicePreference::GPU:  return "GPU";
    case ComputeDevicePreference::CPU:  return "CPU";
    }

    return "Auto";
}

struct GenerationTargetProfile
{
    std::string_view displayName;
    std::string_view secondaryLabel;
    std::string_view secondaryPlaceholder;
    std::string_view suggestedTrackName;
    ModelCapability capability{ModelCapability::TrackGeneration};
    bool secondaryPromptRepresentsLyrics{false};
};

inline constexpr std::array<GenerationTarget, 14> kGenerationTargets{
    GenerationTarget::Song,
    GenerationTarget::Drums,
    GenerationTarget::Bass,
    GenerationTarget::Guitar,
    GenerationTarget::Keyboard,
    GenerationTarget::Percussion,
    GenerationTarget::Strings,
    GenerationTarget::Synth,
    GenerationTarget::FX,
    GenerationTarget::Brass,
    GenerationTarget::Woodwinds,
    GenerationTarget::Vocals,
    GenerationTarget::BackingVocals,
    GenerationTarget::Custom,
};

inline constexpr const std::array<GenerationTarget, 14>& kMusicGenerationCategories = kGenerationTargets;

inline constexpr GenerationTargetProfile generationTargetProfile(GenerationTarget target) noexcept
{
    switch (target)
    {
    case GenerationTarget::Drums:
        return {"Drums", "Rhythm Notes", "Describe groove, rhythm, kit feel, punch, swing...", "Drums", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Bass:
        return {"Bass", "Bassline Notes", "Describe bass groove, rhythm, tone, movement...", "Bass", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Guitar:
        return {"Guitar", "Riff Notes", "Describe riff, picking, strumming, tone, articulation...", "Guitar", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Keyboard:
        return {"Keyboard", "Keyboard Notes", "Describe chords, rhythm, voicing, texture...", "Keyboard", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Percussion:
        return {"Percussion", "Percussion Notes", "Describe percussion accents, groove, movement, transient feel...", "Percussion", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Strings:
        return {"Strings", "String Notes", "Describe arrangement, sustain, staccato, emotion...", "Strings", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Synth:
        return {"Synth", "Texture Notes", "Describe synth texture, motion, layer, atmosphere...", "Synth", ModelCapability::TrackGeneration, false};
    case GenerationTarget::FX:
        return {"FX", "FX Notes", "Describe risers, impacts, sweeps, glitches, transitions...", "FX", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Brass:
        return {"Brass", "Brass Notes", "Describe brass arrangement, stabs, swells, section feel...", "Brass", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Woodwinds:
        return {"Woodwinds", "Woodwind Notes", "Describe woodwind phrasing, breath, movement, texture...", "Woodwinds", ModelCapability::TrackGeneration, false};
    case GenerationTarget::Vocals:
        return {"Vocals", "Lyrics", "Write vocal lyrics...", "Vocals", ModelCapability::TrackGeneration, true};
    case GenerationTarget::BackingVocals:
        return {"Backing Vocals", "Lyrics / Vocal Notes", "Add backing vocal lyrics, syllables, harmonies, choir notes...", "Backing Vocals", ModelCapability::TrackGeneration, true};
    case GenerationTarget::Song:
        return {"Song", "Lyrics", "Write lyrics for the song...", "Song", ModelCapability::SongGeneration, true};
    case GenerationTarget::Custom:
        return {"Custom", "Custom Instructions", "Describe exactly what to generate...", "Generated Layer", ModelCapability::TrackGeneration, false};
    }

    return {"Song", "Lyrics", "Write lyrics for the song...", "Song", ModelCapability::SongGeneration, true};
}

inline constexpr std::string_view musicGenerationCategoryLabel(GenerationTarget target) noexcept
{
    return generationTargetProfile(target).displayName;
}

inline constexpr ModelCapability generationTargetCapability(GenerationTarget target) noexcept
{
    return generationTargetProfile(target).capability;
}

struct MusicGenerationRequest
{
    GenerationTarget category{GenerationTarget::Song};
    ComputeDevicePreference devicePreference{ComputeDevicePreference::Auto};
    std::string stylesPrompt;
    std::string secondaryPrompt;
    std::string lyricsPrompt;
    bool isInstrumental{true};
    bool secondaryPromptIsLyrics{true};
    std::string selectedModel;
    std::string selectedModelDisplayName;
    std::string selectedModelPath;
    std::string selectedModelVersion;
    double durationSec{12.0};
    double bpm{0.0};
    int seed{0};
    std::string musicalKey;
};
}
