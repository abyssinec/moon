#pragma once

#include "AppController.h"

#if MOON_HAS_JUCE
#include <memory>
#include <utility>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "InspectorPanel.h"
#include "TaskPanel.h"
#include "TimelineView.h"
#include "TrackListView.h"
#include "TransportBar.h"

namespace moon::app
{
class SyncViewport final : public juce::Viewport
{
public:
    std::function<void(const juce::Rectangle<int>&)> onVisibleAreaChanged;

private:
    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override
    {
        if (onVisibleAreaChanged)
        {
            onVisibleAreaChanged(newVisibleArea);
        }
    }
};

class TimelineRulerBar final : public juce::Component
{
public:
    TimelineRulerBar(std::function<double()> pixelsPerSecondProvider,
                     std::function<int()> scrollXProvider,
                     std::function<double()> tempoProvider,
                     std::function<std::pair<int, int>()> timeSignatureProvider,
                     std::function<moon::ui::TimelineGridMode()> gridModeProvider,
                     std::function<double()> playheadProvider,
                     std::function<void(double)> seekCallback)
        : pixelsPerSecondProvider_(std::move(pixelsPerSecondProvider))
        , scrollXProvider_(std::move(scrollXProvider))
        , tempoProvider_(std::move(tempoProvider))
        , timeSignatureProvider_(std::move(timeSignatureProvider))
        , gridModeProvider_(std::move(gridModeProvider))
        , playheadProvider_(std::move(playheadProvider))
        , seekCallback_(std::move(seekCallback))
    {
    }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void repaintPlayheadDelta(double previousPlayheadSec, double nextPlayheadSec);

private:
    std::function<double()> pixelsPerSecondProvider_;
    std::function<int()> scrollXProvider_;
    std::function<double()> tempoProvider_;
    std::function<std::pair<int, int>()> timeSignatureProvider_;
    std::function<moon::ui::TimelineGridMode()> gridModeProvider_;
    std::function<double()> playheadProvider_;
    std::function<void(double)> seekCallback_;
};

class TimelineOverviewBar final : public juce::Component
{
public:
    TimelineOverviewBar(std::function<double()> totalDurationProvider,
                        std::function<int()> contentWidthProvider,
                        std::function<int()> viewWidthProvider,
                        std::function<int()> scrollXProvider,
                        std::function<void(int)> setScrollXCallback)
        : totalDurationProvider_(std::move(totalDurationProvider))
        , contentWidthProvider_(std::move(contentWidthProvider))
        , viewWidthProvider_(std::move(viewWidthProvider))
        , scrollXProvider_(std::move(scrollXProvider))
        , setScrollXCallback_(std::move(setScrollXCallback))
    {
    }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void repaintViewportDelta(int previousScrollX, int nextScrollX, int contentWidth, int viewWidth);

private:
    void updateScrollFromPosition(int x, bool keepDragOffset);
    std::function<double()> totalDurationProvider_;
    std::function<int()> contentWidthProvider_;
    std::function<int()> viewWidthProvider_;
    std::function<int()> scrollXProvider_;
    std::function<void(int)> setScrollXCallback_;
    bool draggingWindow_{false};
    int dragOffsetX_{0};
};

class MainComponent final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit MainComponent(AppController& controller);
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void createNewProject();
    void openExistingProject();
    void saveCurrentProject();
    void importAudioFile();
    void exportFullMixFile();
    void exportSelectedRegionFile();
    void exportStemTracksFiles();
    void refreshBackendConnection();
    void rebuildPreviewCache();
    void editSettings();
    void showMainMenu();
    void refreshActionAvailability();
    void refreshProjectStatusLabel();
    void showOperationFailure(const juce::String& title, const juce::String& fallbackMessage, std::size_t previousLogCount);
    void showOperationInfo(const juce::String& title, const juce::String& message);
    void refreshScrollableContentSizes();
    void syncTrackStripScroll();
    std::string resolveDropTrackId(int y) const;
    double resolveDropStartSec(int x) const;
    void updateDropHover(int x, int y);
    void clearDropHover();
    void showGridModeMenu();
    void refreshTimelineTuningWidgets();
    void refreshFpsCounter(double deltaSec);
    void repaintPlayheadPresentation(double previousPlayheadSec, double nextPlayheadSec, bool forceFull);
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

    AppController& controller_;
    juce::AudioDeviceManager audioDeviceManager_;
    std::unique_ptr<juce::AudioSourcePlayer> audioSourcePlayer_;
    juce::TextButton menuButton_{"Menu"};
    juce::Label tracksHeaderLabel_;
    juce::TextButton addTrackButton_{"+ Add Track"};
    juce::TextButton newProjectButton_{"New"};
    juce::TextButton openProjectButton_{"Open"};
    juce::TextButton saveProjectButton_{"Save"};
    juce::TextButton importAudioButton_{"Import WAV"};
    juce::TextButton refreshBackendButton_{"Refresh Backend"};
    juce::TextButton rebuildPreviewButton_{"Rebuild Preview"};
    juce::TextButton zoomOutButton_{"Zoom -"};
    juce::TextButton zoomResetButton_{"Zoom 1:1"};
    juce::TextButton zoomInButton_{"Zoom +"};
    juce::TextButton settingsButton_{"Settings"};
    juce::Label projectStatusLabel_;
    juce::TextButton gridModeButton_{"Beat"};
    juce::Label zoomValueLabel_;
    juce::Label fpsValueLabel_;
    juce::Label waveformDetailLabel_;
    juce::Slider waveformDetailSlider_;
    juce::TextButton exportMixButton_{"Export Mix"};
    juce::TextButton exportRegionButton_{"Export Region"};
    juce::TextButton exportStemsButton_{"Export Stems"};
    juce::Label startupNoticeLabel_;
    juce::TextButton dismissStartupNoticeButton_{"Dismiss"};
    int autosaveTickCounter_{0};
    int taskPollTickCounter_{0};
    double lastTransportTickMs_{0.0};
    double fpsSmoothed_{60.0};
    double lastTimelinePixelsPerSecond_{100.0};
    double lastPresentedPlayheadSec_{0.0};
    bool syncingTrackScroll_{false};
    int lastTimelineScrollY_{0};
    int lastTrackScrollY_{0};
    int lastTimelineContentWidth_{0};
    int lastTimelineViewportWidth_{0};
    moon::ui::TransportBar transportBar_;
    moon::ui::TrackListView trackListView_;
    SyncViewport trackListViewport_;
    SyncViewport timelineViewport_;
    TimelineRulerBar timelineRulerBar_;
    TimelineOverviewBar timelineOverviewBar_;
    moon::ui::TimelineView timelineView_;
    juce::Viewport inspectorViewport_;
    moon::ui::InspectorPanel inspectorPanel_;
    juce::Viewport taskViewport_;
    moon::ui::TaskPanel taskPanel_;
    std::unique_ptr<juce::FileChooser> activeFileChooser_;
};
}
#else
namespace moon::app
{
class MainComponent
{
public:
    explicit MainComponent(AppController&) {}
};
}
#endif
