#pragma once

#include <functional>

#include "ProjectManager.h"
#include "TimelineFacade.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class TrackListView final : public juce::Component
{
public:
    TrackListView(moon::engine::TimelineFacade& timeline,
                  moon::engine::ProjectManager& projectManager,
                  std::function<void()> onTrackMixChanged);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class DragMode
    {
        None,
        Gain,
        Pan
    };

    juce::Rectangle<int> rowBounds(int rowIndex) const;
    juce::Rectangle<int> muteButtonBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> soloButtonBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> gainSliderBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> panSliderBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> addTrackBounds() const;
    void applyTrackGainFromPosition(moon::engine::TrackInfo& track, int x, const juce::Rectangle<int>& bounds);
    void applyTrackPanFromPosition(moon::engine::TrackInfo& track, int x, const juce::Rectangle<int>& bounds);

    moon::engine::TimelineFacade& timeline_;
    moon::engine::ProjectManager& projectManager_;
    std::function<void()> onTrackMixChanged_;
    DragMode dragMode_{DragMode::None};
    std::string dragTrackId_;
};
}
#else
namespace moon::ui
{
class TrackListView
{
public:
    TrackListView(moon::engine::TimelineFacade&, moon::engine::ProjectManager&, std::function<void()>) {}
};
}
#endif
