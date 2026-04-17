#pragma once

#include "ProjectManager.h"
#include "TimelineFacade.h"
#include "TransportFacade.h"
#include "WaveformService.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class TimelineView final : public juce::Component
{
public:
    TimelineView(moon::engine::TimelineFacade& timeline,
                 moon::engine::ProjectManager& projectManager,
                 moon::engine::WaveformService& waveformService,
                 moon::engine::TransportFacade& transport,
                 std::function<void(double)> seekTimelineCallback,
                 std::function<void()> beginClipDragCallback,
                 std::function<void(bool)> endClipDragCallback);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    double xToTime(int x) const;
    bool isRulerHit(const juce::Point<int>& point) const;
    int clipYForIndex(int index) const;
    juce::Rectangle<int> clipBounds(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip) const;

    moon::engine::TimelineFacade& timeline_;
    moon::engine::ProjectManager& projectManager_;
    moon::engine::WaveformService& waveformService_;
    moon::engine::TransportFacade& transport_;
    std::function<void(double)> seekTimelineCallback_;
    std::function<void()> beginClipDragCallback_;
    std::function<void(bool)> endClipDragCallback_;
    bool draggingPlayhead_{false};
    bool draggingRegion_{false};
    bool draggingClip_{false};
    bool clipMovedDuringDrag_{false};
    double dragStartSec_{0.0};
    double clipDragOffsetSec_{0.0};
    std::string draggedClipId_;
};
}
#else
namespace moon::ui
{
class TimelineView
{
public:
    TimelineView(moon::engine::TimelineFacade&,
                 moon::engine::ProjectManager&,
                 moon::engine::WaveformService&,
                 moon::engine::TransportFacade&,
                 std::function<void(double)>,
                 std::function<void()>,
                 std::function<void(bool)>) {}
};
}
#endif
