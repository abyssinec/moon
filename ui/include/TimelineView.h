#pragma once

#include "ProjectManager.h"
#include "TimelineFacade.h"
#include "TransportFacade.h"
#include "WaveformRenderer.h"
#include "WaveformService.h"
#include "TimelineLayout.h"
#include <optional>

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
enum class TimelineGridMode
{
    None,
    StepDiv6,
    StepDiv4,
    StepDiv3,
    StepDiv2,
    Step,
    BeatDiv6,
    BeatDiv4,
    BeatDiv3,
    BeatDiv2,
    Beat,
    Bar
};

class TimelineView final : public juce::Component, private moon::engine::WaveformService::Listener
{
public:
    TimelineView(moon::engine::TimelineFacade& timeline,
                 moon::engine::ProjectManager& projectManager,
                 moon::engine::WaveformService& waveformService,
                 moon::engine::TransportFacade& transport,
                 std::function<void(double)> seekTimelineCallback,
                 std::function<void()> beginClipDragCallback,
                 std::function<void(bool)> endClipDragCallback,
                 std::function<void()> splitClipCallback,
                 std::function<void()> deleteClipCallback,
                 std::function<bool(double)> setFadeInCallback,
                 std::function<bool(double)> setFadeOutCallback,
                 std::function<bool(double)> trimLeftCallback,
                 std::function<bool(double)> trimRightCallback);
    ~TimelineView() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void setPixelsPerSecond(double value);
    void setPixelsPerSecond(double value, std::optional<double> anchorTimelineSec, std::optional<int> anchorViewX = std::nullopt);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void repaintPlayheadDelta(double previousPlayheadSec, double nextPlayheadSec);
    double pixelsPerSecond() const noexcept { return pixelsPerSecond_; }
    void setGridMode(TimelineGridMode mode);
    TimelineGridMode gridMode() const noexcept { return gridMode_; }
    void setWaveformDetailScale(double value);
    double waveformDetailScale() const noexcept { return waveformDetailScale_; }
    void updateContentSize();
    std::string nearestTrackIdForY(int contentY) const;
    double timeForContentX(int contentX) const;
    void setDropHoverTrackId(const std::string& trackId);

private:
    class WaveformLayerComponent final : public juce::Component
    {
    public:
        explicit WaveformLayerComponent(TimelineView& owner) : owner_(owner)
        {
            setInterceptsMouseClicks(false, false);
        }

        void paint(juce::Graphics& g) override
        {
            owner_.paintTimelineBase(g);
            owner_.paintWaveformLayer(g);
        }

    private:
        TimelineView& owner_;
    };

    enum class ClipDragMode
    {
        None,
        Move,
        TrimLeft,
        TrimRight,
        FadeIn,
        FadeOut
    };

    double xToTime(int x) const;
    bool isRulerHit(const juce::Point<int>& point) const;
    int clipYForIndex(int index) const;
    juce::Rectangle<int> clipBounds(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip) const;
    void paintTimelineBase(juce::Graphics& g);
    void paintWaveformLayer(juce::Graphics& g);
    void paintClipOverlays(juce::Graphics& g);
    ClipDragMode hitTestClipDragMode(const juce::Rectangle<int>& clipArea, const juce::Point<int>& point) const;
    moon::engine::ClipInfo* draggedClip(moon::engine::ProjectState& state);
    juce::Colour trackAccentForClip(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip, bool selected) const;
    void waveformSourceUpdated(const std::string& path) override;
    void repaintWaveformSource(const std::string& path);

    moon::engine::TimelineFacade& timeline_;
    moon::engine::ProjectManager& projectManager_;
    moon::engine::WaveformService& waveformService_;
    moon::engine::TransportFacade& transport_;
    std::function<void(double)> seekTimelineCallback_;
    std::function<void()> beginClipDragCallback_;
    std::function<void(bool)> endClipDragCallback_;
    std::function<void()> splitClipCallback_;
    std::function<void()> deleteClipCallback_;
    std::function<bool(double)> setFadeInCallback_;
    std::function<bool(double)> setFadeOutCallback_;
    std::function<bool(double)> trimLeftCallback_;
    std::function<bool(double)> trimRightCallback_;
    WaveformLayerComponent waveformLayer_{*this};
    bool draggingPlayhead_{false};
    bool draggingRegion_{false};
    bool draggingClip_{false};
    bool clipMovedDuringDrag_{false};
    double dragStartSec_{0.0};
    double clipDragOffsetSec_{0.0};
    double lastDragTimelineSec_{0.0};
    double pixelsPerSecond_{100.0};
    TimelineGridMode gridMode_{TimelineGridMode::Beat};
    double waveformDetailScale_{0.55};
    std::string draggedClipId_;
    std::string dropHoverTrackId_;
    ClipDragMode clipDragMode_{ClipDragMode::None};
    WaveformTileCache waveformTileCache_;
    WaveformRendererOpenGL waveformRendererOpenGL_;
    WaveformTileCache::DrawBatch lastWaveformBatch_;
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
                 std::function<void(bool)>,
                 std::function<void()>,
                 std::function<void()>,
                 std::function<bool(double)>,
                 std::function<bool(double)>,
                 std::function<bool(double)>,
                 std::function<bool(double)>) {}
};
}
#endif
