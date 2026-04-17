#include "RegionSelectionOverlay.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
void RegionSelectionOverlay::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::yellow.withAlpha(0.2f));
    g.fillAll();
}
}
#endif
