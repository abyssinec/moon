#pragma once

#include <array>
#include <string>
#include <string_view>

namespace moon::engine
{
enum class MusicGenerationCategory
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

struct MusicGenerationRequest
{
    MusicGenerationCategory category{MusicGenerationCategory::Song};
    std::string stylesPrompt;
    std::string lyricsPrompt;
    bool isInstrumental{true};
    std::string selectedModel;
    double durationSec{12.0};
    double bpm{0.0};
    int seed{0};
    std::string musicalKey;
};

inline constexpr std::array<MusicGenerationCategory, 14> kMusicGenerationCategories{
    MusicGenerationCategory::Drums,
    MusicGenerationCategory::Bass,
    MusicGenerationCategory::Guitar,
    MusicGenerationCategory::Keyboard,
    MusicGenerationCategory::Percussion,
    MusicGenerationCategory::Strings,
    MusicGenerationCategory::Synth,
    MusicGenerationCategory::FX,
    MusicGenerationCategory::Brass,
    MusicGenerationCategory::Woodwinds,
    MusicGenerationCategory::Vocals,
    MusicGenerationCategory::BackingVocals,
    MusicGenerationCategory::Song,
    MusicGenerationCategory::Custom,
};

inline constexpr std::string_view musicGenerationCategoryLabel(MusicGenerationCategory category) noexcept
{
    switch (category)
    {
    case MusicGenerationCategory::Drums:          return "Drums";
    case MusicGenerationCategory::Bass:           return "Bass";
    case MusicGenerationCategory::Guitar:         return "Guitar";
    case MusicGenerationCategory::Keyboard:       return "Keyboard";
    case MusicGenerationCategory::Percussion:     return "Percussion";
    case MusicGenerationCategory::Strings:        return "Strings";
    case MusicGenerationCategory::Synth:          return "Synth";
    case MusicGenerationCategory::FX:             return "FX";
    case MusicGenerationCategory::Brass:          return "Brass";
    case MusicGenerationCategory::Woodwinds:      return "Woodwinds";
    case MusicGenerationCategory::Vocals:         return "Vocals";
    case MusicGenerationCategory::BackingVocals:  return "Backing Vocals";
    case MusicGenerationCategory::Song:           return "Song";
    case MusicGenerationCategory::Custom:         return "Custom";
    }

    return "Song";
}
}
