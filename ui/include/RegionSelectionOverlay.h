#pragma once

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class RegionSelectionOverlay final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override;
};
}
#else
namespace moon::ui
{
class RegionSelectionOverlay {};
}
#endif
