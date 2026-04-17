#pragma once

#include "ProjectManager.h"
#include "TimelineFacade.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class TrackListView final : public juce::Component
{
public:
    TrackListView(moon::engine::TimelineFacade& timeline, moon::engine::ProjectManager& projectManager);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    juce::Rectangle<int> rowBounds(int rowIndex) const;
    juce::Rectangle<int> muteButtonBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> soloButtonBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> addTrackBounds() const;

    moon::engine::TimelineFacade& timeline_;
    moon::engine::ProjectManager& projectManager_;
};
}
#else
namespace moon::ui
{
class TrackListView
{
public:
    TrackListView(moon::engine::TimelineFacade&, moon::engine::ProjectManager&) {}
};
}
#endif
