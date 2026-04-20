#pragma once

#include <functional>
#include <string>

#include "ProjectManager.h"
#include "TimelineFacade.h"
#include "TimelineLayout.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class TrackListView final : public juce::Component
{
public:
    TrackListView(moon::engine::TimelineFacade& timeline,
                  moon::engine::ProjectManager& projectManager,
                  std::function<void()> onTrackMixChanged,
                  std::function<bool(const std::string&, const std::string&)> onTrackRenamed,
                  std::function<bool(const std::string&)> onTrackDeleted,
                  std::function<bool(const std::string&, const std::string&)> onTrackColorChanged);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void resized() override;
    void setDropHoverTrackId(const std::string& trackId);

private:
    enum class DragMode
    {
        None,
        Gain,
        Pan
    };

    juce::Rectangle<int> rowBounds(int rowIndex) const;
    juce::Rectangle<int> muteButtonBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> accentStripBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> soloButtonBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> nameBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> gainSliderBounds(const juce::Rectangle<int>& row) const;
    juce::Rectangle<int> panSliderBounds(const juce::Rectangle<int>& row) const;
    void applyTrackGainFromPosition(moon::engine::TrackInfo& track, int x, const juce::Rectangle<int>& bounds);
    void applyTrackPanFromPosition(moon::engine::TrackInfo& track, int x, const juce::Rectangle<int>& bounds);
    void beginRename(const moon::engine::TrackInfo& track, int rowIndex);
    void commitRename();
    void cancelRename();
    void showTrackColorMenu(const moon::engine::TrackInfo& track, const juce::Rectangle<int>& swatchBounds);

    moon::engine::TimelineFacade& timeline_;
    moon::engine::ProjectManager& projectManager_;
    std::function<void()> onTrackMixChanged_;
    std::function<bool(const std::string&, const std::string&)> onTrackRenamed_;
    std::function<bool(const std::string&)> onTrackDeleted_;
    std::function<bool(const std::string&, const std::string&)> onTrackColorChanged_;
    DragMode dragMode_{DragMode::None};
    std::string dragTrackId_;
    std::string dropHoverTrackId_;
    std::string renamingTrackId_;
    std::string renameOriginalName_;
    int renamingRowIndex_{-1};
    juce::TextEditor renameEditor_;
};
}
#else
namespace moon::ui
{
class TrackListView
{
public:
    TrackListView(moon::engine::TimelineFacade&, moon::engine::ProjectManager&, std::function<void()>, std::function<bool(const std::string&, const std::string&)>, std::function<bool(const std::string&)>, std::function<bool(const std::string&, const std::string&)>) {}
};
}
#endif
