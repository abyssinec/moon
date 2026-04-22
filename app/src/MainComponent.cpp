#include "MainComponent.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#if MOON_HAS_JUCE
namespace moon::app
{
namespace
{
void appendMainComponentTrace(const std::string& line)
{
    if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
    {
        const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::app);
        if (out)
        {
            out << line << "\n";
        }
    }
}

double beatDurationSec(double tempo, int denominator)
{
    const double quarterNoteSec = 60.0 / juce::jmax(30.0, tempo);
    return quarterNoteSec * (4.0 / static_cast<double>(juce::jmax(1, denominator)));
}

double barDurationSec(double tempo, int numerator, int denominator)
{
    return beatDurationSec(tempo, denominator) * static_cast<double>(juce::jmax(1, numerator));
}

double gridDivisionSec(moon::ui::TimelineGridMode mode, double tempo, int numerator, int denominator)
{
    const auto beatSec = beatDurationSec(tempo, denominator);
    const auto barSec = barDurationSec(tempo, numerator, denominator);
    switch (mode)
    {
    case moon::ui::TimelineGridMode::None:      return 0.0;
    case moon::ui::TimelineGridMode::StepDiv6:  return beatSec / 24.0;
    case moon::ui::TimelineGridMode::StepDiv4:  return beatSec / 16.0;
    case moon::ui::TimelineGridMode::StepDiv3:  return beatSec / 12.0;
    case moon::ui::TimelineGridMode::StepDiv2:  return beatSec / 8.0;
    case moon::ui::TimelineGridMode::Step:      return beatSec / 4.0;
    case moon::ui::TimelineGridMode::BeatDiv6:  return beatSec / 6.0;
    case moon::ui::TimelineGridMode::BeatDiv4:  return beatSec / 4.0;
    case moon::ui::TimelineGridMode::BeatDiv3:  return beatSec / 3.0;
    case moon::ui::TimelineGridMode::BeatDiv2:  return beatSec / 2.0;
    case moon::ui::TimelineGridMode::Beat:      return beatSec;
    case moon::ui::TimelineGridMode::Bar:       return barSec;
    }

    return beatSec;
}

struct TimelineRulerLod
{
    int barLabelStride { 1 };
    bool showBeatNumbers { false };
    bool showBeatLines { true };
    bool showSubdivisionLines { false };
    float majorLineThickness { 1.45f };
    float beatLineThickness { 0.90f };
    float subdivisionLineThickness { 0.55f };
    float bandAlpha { 1.0f };
};

TimelineRulerLod computeTimelineRulerLod(float barWidthPx, float beatWidthPx, float gridWidthPx)
{
    TimelineRulerLod lod{};

    if (barWidthPx < 28.0f)
    {
        lod.barLabelStride = 8;
        lod.showBeatNumbers = false;
        lod.showBeatLines = false;
        lod.showSubdivisionLines = false;
        lod.majorLineThickness = 1.20f;
        lod.beatLineThickness = 0.0f;
        lod.subdivisionLineThickness = 0.0f;
        lod.bandAlpha = 0.58f;
        return lod;
    }

    if (barWidthPx < 42.0f)
    {
        lod.barLabelStride = 4;
        lod.showBeatNumbers = false;
        lod.showBeatLines = false;
        lod.showSubdivisionLines = false;
        lod.majorLineThickness = 1.24f;
        lod.beatLineThickness = 0.0f;
        lod.subdivisionLineThickness = 0.0f;
        lod.bandAlpha = 0.66f;
        return lod;
    }

    if (barWidthPx < 72.0f)
    {
        lod.barLabelStride = 2;
        lod.showBeatNumbers = false;
        lod.showBeatLines = true;
        lod.showSubdivisionLines = false;
        lod.majorLineThickness = 1.32f;
        lod.beatLineThickness = 0.68f;
        lod.subdivisionLineThickness = 0.0f;
        lod.bandAlpha = 0.76f;
        return lod;
    }

    if (barWidthPx < 148.0f)
    {
        lod.barLabelStride = 1;
        lod.showBeatNumbers = false;
        lod.showBeatLines = true;
        lod.showSubdivisionLines = gridWidthPx >= 13.0f;
        lod.majorLineThickness = 1.42f;
        lod.beatLineThickness = 0.82f;
        lod.subdivisionLineThickness = 0.42f;
        lod.bandAlpha = 0.90f;
        return lod;
    }

    lod.barLabelStride = 1;
    lod.showBeatNumbers = beatWidthPx >= 34.0f;
    lod.showBeatLines = true;
    lod.showSubdivisionLines = gridWidthPx >= 10.0f;
    lod.majorLineThickness = 1.52f;
    lod.beatLineThickness = 0.92f;
    lod.subdivisionLineThickness = 0.50f;
    lod.bandAlpha = 1.0f;
    return lod;
}

juce::String gridModeLabel(moon::ui::TimelineGridMode mode)
{
    switch (mode)
    {
    case moon::ui::TimelineGridMode::None:     return "(none)";
    case moon::ui::TimelineGridMode::StepDiv6: return "1/6 step";
    case moon::ui::TimelineGridMode::StepDiv4: return "1/4 step";
    case moon::ui::TimelineGridMode::StepDiv3: return "1/3 step";
    case moon::ui::TimelineGridMode::StepDiv2: return "1/2 step";
    case moon::ui::TimelineGridMode::Step:     return "Step";
    case moon::ui::TimelineGridMode::BeatDiv6: return "1/6 beat";
    case moon::ui::TimelineGridMode::BeatDiv4: return "1/4 beat";
    case moon::ui::TimelineGridMode::BeatDiv3: return "1/3 beat";
    case moon::ui::TimelineGridMode::BeatDiv2: return "1/2 beat";
    case moon::ui::TimelineGridMode::Beat:     return "Beat";
    case moon::ui::TimelineGridMode::Bar:      return "Bar";
    }

    return "Beat";
}

void repaintSeekViews(moon::ui::TimelineView& timelineView,
                      TimelineRulerBar& rulerBar,
                      double previousPlayheadSec,
                      double nextPlayheadSec,
                      bool forceFull)
{
    if (forceFull)
    {
        timelineView.repaint();
        rulerBar.repaint();
        return;
    }

    timelineView.repaintPlayheadDelta(previousPlayheadSec, nextPlayheadSec);
    rulerBar.repaintPlayheadDelta(previousPlayheadSec, nextPlayheadSec);
}
}

void TimelineRulerBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(10, 14, 18));

    const auto pixelsPerSecond = juce::jmax(1.0, pixelsPerSecondProvider_());
    const auto tempo = juce::jmax(30.0, tempoProvider_());
    const auto timeSignature = timeSignatureProvider_ ? timeSignatureProvider_() : std::pair<int, int>{4, 4};
    const auto numerator = juce::jmax(1, timeSignature.first);
    const auto denominator = juce::jmax(1, timeSignature.second);
    const auto scrollX = scrollXProvider_();
    const auto playheadSec = juce::jmax(0.0, playheadProvider_());

    const double barSec = barDurationSec(tempo, numerator, denominator);
    const float barWidthPx = static_cast<float>(barSec * pixelsPerSecond);

    const auto maxTimelineSec = (scrollX + getWidth()) / pixelsPerSecond;
    const auto firstBarIndex = juce::jmax(0, static_cast<int>(std::floor(static_cast<double>(scrollX) / pixelsPerSecond / barSec)) - 1);
    const auto lastBarIndex = juce::jmax(firstBarIndex, static_cast<int>(std::ceil(maxTimelineSec / barSec)) + 1);

    int barLabelStride = 1;
    if (barWidthPx < 28.0f)      { barLabelStride = 8; }
    else if (barWidthPx < 42.0f) { barLabelStride = 4; }
    else if (barWidthPx < 72.0f) { barLabelStride = 2; }

    for (int barIndex = firstBarIndex; barIndex <= lastBarIndex; ++barIndex)
    {
        if ((barIndex % barLabelStride) != 0)
        {
            continue;
        }

        const int centerX = static_cast<int>(std::round(static_cast<double>(barIndex) * barSec * pixelsPerSecond)) - scrollX;
        if (centerX < -40 || centerX > getWidth() + 40)
        {
            continue;
        }

        const auto labelValue = barIndex + 1;
        const int labelWidth = juce::jlimit(18, 56, static_cast<int>(barWidthPx) - 4);
        const int labelX = centerX - (labelWidth / 2);

        g.setColour(juce::Colours::white.withAlpha(0.94f));
        g.setFont(juce::FontOptions(barWidthPx >= 120.0f ? 13.0f : (barWidthPx >= 54.0f ? 11.5f : 10.0f), juce::Font::bold));
        g.drawText(juce::String(labelValue), labelX, 5, labelWidth, 18, juce::Justification::centred);
    }

    const auto playheadX = static_cast<float>(playheadSec * pixelsPerSecond - scrollX);
    g.setColour(juce::Colour::fromRGB(255, 182, 32));
    g.drawLine(playheadX, 7.0f, playheadX, static_cast<float>(getHeight()), 1.6f);

    juce::Path marker;
    marker.startNewSubPath(playheadX - 5.0f, 0.0f);
    marker.lineTo(playheadX + 5.0f, 0.0f);
    marker.lineTo(playheadX, 7.0f);
    marker.closeSubPath();
    g.fillPath(marker);
}

void TimelineRulerBar::mouseDown(const juce::MouseEvent& event)
{
    if (seekCallback_)
    {
        const auto timelineSec = (static_cast<double>(event.x) + static_cast<double>(scrollXProvider_())) / juce::jmax(1.0, pixelsPerSecondProvider_());
        seekCallback_(juce::jmax(0.0, timelineSec));
    }
}

void TimelineRulerBar::mouseDrag(const juce::MouseEvent& event)
{
    mouseDown(event);
}

void TimelineRulerBar::repaintPlayheadDelta(double previousPlayheadSec, double nextPlayheadSec)
{
    const auto pixelsPerSecond = juce::jmax(1.0, pixelsPerSecondProvider_());
    const auto scrollX = scrollXProvider_();
    const auto previousX = static_cast<int>(std::round(previousPlayheadSec * pixelsPerSecond)) - scrollX;
    const auto nextX = static_cast<int>(std::round(nextPlayheadSec * pixelsPerSecond)) - scrollX;
    const auto dirtyLeft = std::min(previousX, nextX) - 8;
    const auto dirtyWidth = std::abs(nextX - previousX) + 16;
    repaint(dirtyLeft, 0, juce::jmax(20, dirtyWidth), getHeight());
}

void TimelineOverviewBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 20));

    const auto totalDurationSec = juce::jmax(1.0, totalDurationProvider_());
    const auto contentWidth = juce::jmax(1, contentWidthProvider_());
    const auto viewWidth = juce::jmax(1, viewWidthProvider_());
    const auto scrollX = juce::jlimit(0, juce::jmax(0, contentWidth - viewWidth), scrollXProvider_());

    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());

    g.setColour(juce::Colour::fromRGB(24, 27, 32));
    g.fillRoundedRectangle(0.0f, 0.0f, width, height, 4.0f);

    const double visibleStartSec = totalDurationSec * (static_cast<double>(scrollX) / static_cast<double>(contentWidth));
    const double visibleDurationSec = juce::jmax(0.1, totalDurationSec * (static_cast<double>(viewWidth) / static_cast<double>(contentWidth)));
    const double visibleEndSec = juce::jmin(totalDurationSec, visibleStartSec + visibleDurationSec);

    const float visibleX = width * static_cast<float>(scrollX) / static_cast<float>(contentWidth);
    const float visibleW = juce::jmax(26.0f, width * static_cast<float>(viewWidth) / static_cast<float>(contentWidth));

    g.setColour(juce::Colour::fromRGB(49, 52, 58).withAlpha(0.82f));
    g.fillRoundedRectangle(visibleX, 2.0f, juce::jmin(visibleW, width - visibleX), height - 4.0f, 4.0f);

    const double rawStep = visibleDurationSec / 8.0;
    double stepSec = 1.0;

    if (rawStep <= 1.0)       { stepSec = 1.0; }
    else if (rawStep <= 2.0)  { stepSec = 2.0; }
    else if (rawStep <= 5.0)  { stepSec = 5.0; }
    else if (rawStep <= 10.0) { stepSec = 10.0; }
    else if (rawStep <= 15.0) { stepSec = 15.0; }
    else if (rawStep <= 30.0) { stepSec = 30.0; }
    else if (rawStep <= 60.0) { stepSec = 60.0; }
    else if (rawStep <= 120.0){ stepSec = 120.0; }
    else                      { stepSec = 300.0; }

    const double firstTickSec = std::floor(visibleStartSec / stepSec) * stepSec;

    for (double sec = firstTickSec; sec <= visibleEndSec + stepSec; sec += stepSec)
    {
        const double localRatio = (sec - visibleStartSec) / visibleDurationSec;
        const float x = static_cast<float>(localRatio * width);

        if (x < -1.0f || x > width + 1.0f)
        {
            continue;
        }

        g.setColour(juce::Colours::white.withAlpha(0.20f));
        g.drawLine(x, height - 9.0f, x, height - 2.0f, 1.0f);

        const int totalSeconds = static_cast<int>(std::round(sec));
        const int minutes = totalSeconds / 60;
        const int seconds = totalSeconds % 60;
        const auto label = juce::String::formatted("%02d:%02d", minutes, seconds);

        g.setColour(juce::Colours::white.withAlpha(0.62f));
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(label, static_cast<int>(x) + 4, 0, 52, 12, juce::Justification::centredLeft);
    }
}

void TimelineOverviewBar::mouseDown(const juce::MouseEvent& event)
{
    const auto contentWidth = juce::jmax(1, contentWidthProvider_());
    const auto viewWidth = juce::jmax(1, viewWidthProvider_());
    const auto scrollX = juce::jlimit(0, juce::jmax(0, contentWidth - viewWidth), scrollXProvider_());
    const float width = static_cast<float>(getWidth());
    const float visibleX = width * static_cast<float>(scrollX) / static_cast<float>(contentWidth);
    const float visibleW = juce::jmax(26.0f, width * static_cast<float>(viewWidth) / static_cast<float>(contentWidth));
    draggingWindow_ = event.x >= static_cast<int>(visibleX) && event.x <= static_cast<int>(visibleX + visibleW);
    dragOffsetX_ = event.x - static_cast<int>(visibleX);
    updateScrollFromPosition(event.x, draggingWindow_);
}

void TimelineOverviewBar::mouseDrag(const juce::MouseEvent& event)
{
    updateScrollFromPosition(event.x, draggingWindow_);
}

void TimelineOverviewBar::mouseUp(const juce::MouseEvent&)
{
    draggingWindow_ = false;
}

void TimelineOverviewBar::repaintViewportDelta(int previousScrollX, int nextScrollX, int contentWidth, int viewWidth)
{
    const auto width = static_cast<float>(juce::jmax(1, getWidth()));
    const auto previousX = width * static_cast<float>(previousScrollX) / static_cast<float>(juce::jmax(1, contentWidth));
    const auto nextX = width * static_cast<float>(nextScrollX) / static_cast<float>(juce::jmax(1, contentWidth));
    const auto visibleW = juce::jmax(26.0f, width * static_cast<float>(viewWidth) / static_cast<float>(juce::jmax(1, contentWidth)));
    const auto dirtyLeft = static_cast<int>(std::floor(std::min(previousX, nextX))) - 4;
    const auto dirtyWidth = static_cast<int>(std::ceil(std::abs(nextX - previousX) + visibleW)) + 8;
    repaint(dirtyLeft, 0, juce::jmax(32, dirtyWidth), getHeight());
}

void TimelineOverviewBar::updateScrollFromPosition(int x, bool keepDragOffset)
{
    const auto contentWidth = juce::jmax(1, contentWidthProvider_());
    const auto viewWidth = juce::jmax(1, viewWidthProvider_());
    const int maxScrollX = juce::jmax(0, contentWidth - viewWidth);
    const float width = static_cast<float>(juce::jmax(1, getWidth()));
    const float visibleW = juce::jmax(26.0f, width * static_cast<float>(viewWidth) / static_cast<float>(contentWidth));
    const float targetLeft = keepDragOffset ? static_cast<float>(x - dragOffsetX_) : static_cast<float>(x) - visibleW * 0.5f;
    const float clampedLeft = juce::jlimit(0.0f, juce::jmax(0.0f, width - visibleW), targetLeft);
    const int newScrollX = static_cast<int>(std::round((clampedLeft / width) * static_cast<float>(contentWidth)));
    setScrollXCallback_(juce::jlimit(0, maxScrollX, newScrollX));
}

MainComponent::MainComponent(AppController& controller)
    : controller_(controller)
    , transportBar_(
          controller.transport(),
          controller.logger(),
          [&controller]() { controller.playTransport(); },
          [&controller]() { controller.pauseTransport(); },
          [&controller]() { controller.stopTransport(); },
          [this, &controller](double timelineSec)
          {
              const auto previousPlayheadSec = controller.projectManager().state().uiState.playheadSec;
              controller.seekTimelinePlayhead(timelineSec);
              const auto nextPlayheadSec = controller.projectManager().state().uiState.playheadSec;
              this->repaintPlayheadPresentation(previousPlayheadSec, nextPlayheadSec, true);
          },
          [&controller]()
          {
              return controller.projectPlaybackDurationSec();
          },
          [&controller]()
          {
              return std::pair<int, int>{controller.projectTimeSignatureNumerator(), controller.projectTimeSignatureDenominator()};
          },
          [&controller]()
          {
              return controller.projectTempo();
          },
          [this, &controller](int numerator, int denominator)
          {
              if (controller.setProjectTimeSignature(numerator, denominator))
              {
                  refreshScrollableContentSizes();
                  timelineView_.repaint();
                  timelineRulerBar_.repaint();
                  timelineOverviewBar_.repaint();
                  transportBar_.refresh();
              }
          },
          [this, &controller](double tempo)
          {
              if (controller.setProjectTempo(tempo))
              {
                  refreshScrollableContentSizes();
                  timelineView_.repaint();
                  timelineRulerBar_.repaint();
                  timelineOverviewBar_.repaint();
                  transportBar_.refresh();
              }
          },
          [&controller]() { return controller.backendStatusSummary(); })
    , trackListView_(
          controller.timeline(),
          controller.projectManager(),
          [&controller]()
          {
              controller.notifyProjectMixChanged();
          },
          [&controller](const std::string& trackId, const std::string& newName)
          {
              return controller.renameTrack(trackId, newName);
          },
          [this, &controller](const std::string& trackId)
          {
              const auto removed = controller.deleteTrack(trackId);
              if (removed)
              {
                  refreshScrollableContentSizes();
                  syncTrackStripScroll();
                  timelineView_.repaint();
                  trackListView_.repaint();
                  inspectorPanel_.repaint();
                  taskPanel_.repaint();
              }
              return removed;
          },
          [&controller](const std::string& trackId, const std::string& colorHex)
          {
              return controller.setTrackColor(trackId, colorHex);
          })
    , timelineView_(
          controller.timeline(),
          controller.projectManager(),
          controller.waveformService(),
          controller.transport(),
          [this, &controller](double timelineSec)
          {
              const auto previousPlayheadSec = controller.projectManager().state().uiState.playheadSec;
              controller.seekTimelinePlayhead(timelineSec);
              const auto nextPlayheadSec = controller.projectManager().state().uiState.playheadSec;
              this->repaintPlayheadPresentation(previousPlayheadSec, nextPlayheadSec, true);
          },
          [&controller]()
          {
              controller.beginInteractiveTimelineEdit();
          },
          [&controller](bool changed)
          {
              controller.finishInteractiveTimelineEdit(changed, true);
          },
          [&controller]
          {
              controller.splitSelectedClipAtPlayhead();
          },
          [&controller]
          {
              controller.deleteSelectedClip();
          },
          [&controller](double fadeSec)
          {
              return controller.setSelectedClipFadeIn(fadeSec);
          },
          [&controller](double fadeSec)
          {
              return controller.setSelectedClipFadeOut(fadeSec);
          },
          [&controller](double deltaSec)
          {
              return controller.trimSelectedClipLeft(deltaSec);
          },
          [&controller](double deltaSec)
          {
              return controller.trimSelectedClipRight(deltaSec);
          })
    , timelineRulerBar_(
          [this]()
          {
              return timelineView_.pixelsPerSecond();
          },
          [this]()
          {
              return timelineViewport_.getViewPositionX();
          },
          [&controller]()
          {
              return controller.projectTempo();
          },
          [&controller]()
          {
              return std::pair<int, int>{controller.projectTimeSignatureNumerator(), controller.projectTimeSignatureDenominator()};
          },
          [this]()
          {
              return timelineView_.gridMode();
          },
          [&controller]()
          {
              return controller.projectManager().state().uiState.playheadSec;
          },
          [this, &controller](double timelineSec)
          {
              const auto previousPlayheadSec = controller.projectManager().state().uiState.playheadSec;
              controller.seekTimelinePlayhead(timelineSec);
              const auto nextPlayheadSec = controller.projectManager().state().uiState.playheadSec;
              this->repaintPlayheadPresentation(previousPlayheadSec, nextPlayheadSec, true);
          })
    , timelineOverviewBar_(
          [&controller]()
          {
              return controller.projectPlaybackDurationSec();
          },
          [this]()
          {
              return timelineView_.getWidth();
          },
          [this]()
          {
              return timelineViewport_.getWidth();
          },
          [this]()
          {
              return timelineViewport_.getViewPositionX();
          },
          [this](int x)
          {
              timelineViewport_.setViewPosition(x, timelineViewport_.getViewPositionY());
          })
    , inspectorPanel_(
          controller.projectManager(),
          controller.tasks(),
          controller.logger(),
          [&controller]() { return std::string {}; },
          [&controller](double gain)
          {
              controller.setSelectedClipGain(gain);
          },
          [&controller](double fadeIn)
          {
              controller.setSelectedClipFadeIn(fadeIn);
          },
          [&controller](double fadeOut)
          {
              controller.setSelectedClipFadeOut(fadeOut);
          },
          [&controller](double deltaSec)
          {
              controller.trimSelectedClipLeft(deltaSec);
          },
          [&controller](double deltaSec)
          {
              controller.trimSelectedClipRight(deltaSec);
          },
          [&controller]
          {
              controller.clearSelectedRegion();
          },
          [this, &controller]
          {
              const auto logCount = controller.logger().lineCount();
              if (!controller.splitSelectedClipAtPlayhead())
              {
                  showOperationFailure("Split Clip", "Unable to split the selected clip at the current playhead.", logCount);
              }
          },
          [this, &controller]
          {
              const auto logCount = controller.logger().lineCount();
              if (!controller.activateSelectedTake())
              {
                  showOperationFailure("Activate Take", "Unable to activate the selected take.", logCount);
              }
          },
          [&controller]
          {
              controller.duplicateSelectedClip();
          },
          [&controller]
          {
              controller.deleteSelectedClip();
          },
          [this, &controller](double overlapSec)
          {
              const auto logCount = controller.logger().lineCount();
              if (!controller.createCrossfadeWithPrevious(overlapSec))
              {
                  showOperationFailure("Crossfade Previous", "Select a clip with a suitable previous clip on the same track.", logCount);
              }
          },
          [this, &controller](double overlapSec)
          {
              const auto logCount = controller.logger().lineCount();
              if (!controller.createCrossfadeWithNext(overlapSec))
              {
                  showOperationFailure("Crossfade Next", "Select a clip with a suitable next clip on the same track.", logCount);
              }
          },
          [this, &controller](const std::string& prompt)
          {
              const auto logCount = controller.logger().lineCount();
              if (!controller.rewriteSelectedRegion(prompt))
              {
                  showOperationFailure("Rewrite Region", "Select a clip and a timeline region before starting rewrite.", logCount);
                  return;
              }
              controller.pollTasks();
              showOperationInfo("Rewrite Region", "Rewrite job queued. Progress will appear in the task panel.");
          },
          [this, &controller](const std::string& prompt)
          {
              const auto logCount = controller.logger().lineCount();
              if (!controller.addGeneratedLayer(prompt))
              {
                  showOperationFailure("Add Layer", "Select a clip and a timeline region before generating a new layer.", logCount);
                  return;
              }
              controller.pollTasks();
              showOperationInfo("Add Layer", "Add-layer job queued. Progress will appear in the task panel.");
          },
          [this, &controller]
          {
              const auto logCount = controller.logger().lineCount();
              if (!controller.separateStemsForSelectedClip())
              {
                  showOperationFailure("Separate Stems", "Select a clip before running stem separation.", logCount);
                  return;
              }
              controller.pollTasks();
              showOperationInfo("Separate Stems", "Stem separation job queued. Progress will appear in the task panel.");
          })
    , taskPanel_(controller.tasks(), controller.logger())
{
    appendMainComponentTrace("[bootstrap] MainComponent ctor begin");
    audioSourcePlayer_ = std::make_unique<juce::AudioSourcePlayer>();
    appendMainComponentTrace("[bootstrap] MainComponent audioSourcePlayer created");
    audioSourcePlayer_->setSource(controller.transport().audioSource());

    addAndMakeVisible(menuButton_);
    addAndMakeVisible(tracksHeaderLabel_);
    addAndMakeVisible(addTrackButton_);
    addAndMakeVisible(startupNoticeLabel_);
    addAndMakeVisible(dismissStartupNoticeButton_);
    addAndMakeVisible(transportBar_);
    addAndMakeVisible(gridModeButton_);
    addAndMakeVisible(zoomValueLabel_);
    addAndMakeVisible(fpsValueLabel_);
    addAndMakeVisible(waveformDetailLabel_);
    addAndMakeVisible(waveformDetailSlider_);
    addAndMakeVisible(aiGenerationPanel_);
    trackListViewport_.setViewedComponent(&trackListView_, false);
    trackListViewport_.setScrollBarsShown(false, false);
    trackListViewport_.onVisibleAreaChanged = [this](const juce::Rectangle<int>&)
    {
        if (!syncingTrackScroll_)
        {
            syncTrackStripScroll();
        }
    };
    addAndMakeVisible(trackListViewport_);
    timelineViewport_.setViewedComponent(&timelineView_, false);
    timelineViewport_.setScrollBarsShown(false, false);
    timelineViewport_.onVisibleAreaChanged = [this](const juce::Rectangle<int>&)
    {
        if (syncingTrackScroll_)
        {
            return;
        }

        syncingTrackScroll_ = true;
        syncTrackStripScroll();
        syncingTrackScroll_ = false;
        timelineRulerBar_.repaint();
        timelineOverviewBar_.repaint();
    };
    addAndMakeVisible(timelineViewport_);
    addAndMakeVisible(timelineRulerBar_);
    addAndMakeVisible(timelineOverviewBar_);
    inspectorViewport_.setViewedComponent(&inspectorPanel_, false);
    inspectorViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(inspectorViewport_);
    taskViewport_.setViewedComponent(&taskPanel_, false);
    taskViewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(taskViewport_);

    auto styleViewportScrollbars = [](juce::Viewport& viewport)
    {
        auto& vertical = viewport.getVerticalScrollBar();
        vertical.setColour(juce::ScrollBar::thumbColourId, juce::Colour::fromRGB(46, 51, 59));
        vertical.setColour(juce::ScrollBar::trackColourId, juce::Colour::fromRGB(19, 22, 27));
        vertical.setColour(juce::ScrollBar::backgroundColourId, juce::Colour::fromRGB(15, 17, 21));

        auto& horizontal = viewport.getHorizontalScrollBar();
        horizontal.setColour(juce::ScrollBar::thumbColourId, juce::Colour::fromRGB(46, 51, 59));
        horizontal.setColour(juce::ScrollBar::trackColourId, juce::Colour::fromRGB(19, 22, 27));
        horizontal.setColour(juce::ScrollBar::backgroundColourId, juce::Colour::fromRGB(15, 17, 21));

        viewport.setScrollBarThickness(8);
    };
    styleViewportScrollbars(trackListViewport_);
    styleViewportScrollbars(timelineViewport_);
    styleViewportScrollbars(inspectorViewport_);
    styleViewportScrollbars(taskViewport_);

    auto styleToolbarButton = [](juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(34, 37, 43));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(43, 169, 237));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.9f));
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    };
    styleToolbarButton(menuButton_);
    styleToolbarButton(newProjectButton_);
    styleToolbarButton(openProjectButton_);
    styleToolbarButton(saveProjectButton_);
    styleToolbarButton(importAudioButton_);
    styleToolbarButton(refreshBackendButton_);
    styleToolbarButton(rebuildPreviewButton_);
    styleToolbarButton(gridModeButton_);
    styleToolbarButton(zoomOutButton_);
    styleToolbarButton(zoomResetButton_);
    styleToolbarButton(zoomInButton_);
    styleToolbarButton(settingsButton_);
    styleToolbarButton(exportMixButton_);
    styleToolbarButton(exportRegionButton_);
    styleToolbarButton(exportStemsButton_);
    styleToolbarButton(dismissStartupNoticeButton_);
    styleToolbarButton(addTrackButton_);
    menuButton_.setButtonText("Menu");
    tracksHeaderLabel_.setText("Tracks", juce::dontSendNotification);
    tracksHeaderLabel_.setJustificationType(juce::Justification::centredLeft);
    tracksHeaderLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
    zoomValueLabel_.setJustificationType(juce::Justification::centredLeft);
    zoomValueLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.86f));
    fpsValueLabel_.setJustificationType(juce::Justification::centredLeft);
    fpsValueLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    waveformDetailLabel_.setJustificationType(juce::Justification::centredLeft);
    waveformDetailLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    waveformDetailSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    waveformDetailSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    waveformDetailSlider_.setRange(15.0, 1000.0, 1.0);
    waveformDetailSlider_.setValue(55.0, juce::dontSendNotification);
    waveformDetailSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGB(48, 53, 60));
    waveformDetailSlider_.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(43, 169, 237));
    waveformDetailSlider_.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(83, 203, 255));
    timelineView_.setWaveformDetailScale(waveformDetailSlider_.getValue() / 100.0);
    waveformDetailSlider_.onValueChange = [this]
    {
        timelineView_.setWaveformDetailScale(waveformDetailSlider_.getValue() / 100.0);
        refreshTimelineTuningWidgets();
    };
    refreshTimelineTuningWidgets();

    aiGenerationPanel_.setModelSelectionCallback(
        [this](moon::engine::ModelCapability capability, const std::string& modelId, std::string& errorMessage)
        {
            if (!controller_.setActiveGenerationModel(capability, modelId, errorMessage))
            {
                return false;
            }

            refreshAIGenerationPanel();
            return true;
        });
    aiGenerationPanel_.setOpenModelManagerCallback(
        [this](moon::engine::ModelCapability capability)
        {
            openModelManagerForCapability(capability);
        });
    aiGenerationPanel_.setCreateCallback(
        [this](const moon::engine::MusicGenerationRequest& request)
        {
            const auto stylesPrompt = juce::String(request.stylesPrompt).trim();
            const auto secondaryPrompt = juce::String(request.secondaryPrompt).trim();
            if (stylesPrompt.isEmpty() && secondaryPrompt.isEmpty())
            {
                return moon::ui::MusicGenerationSubmission{false, {}, "Enter style or notes"};
            }

            if (const auto jobId = controller_.generateMusic(request); jobId.has_value())
            {
                taskPanel_.repaint();
                refreshAIGenerationPanelTaskState();
                return moon::ui::MusicGenerationSubmission{true, *jobId, {}};
            }

            const auto error = controller_.lastMusicGenerationError();
            return moon::ui::MusicGenerationSubmission{
                false,
                {},
                error.empty() ? std::string("Music generation could not start") : error};
        });
    aiGenerationPanel_.setCancelCallback(
        [this](const std::string& jobId)
        {
            const auto cancelled = controller_.cancelTask(jobId);
            if (cancelled)
            {
                taskPanel_.repaint();
                refreshAIGenerationPanelTaskState();
            }
            return cancelled;
        });
    aiGenerationPanel_.onHeightChanged = [this]
    {
        resized();
        repaint();
    };
    refreshAIGenerationPanel();

    menuButton_.onClick = [this] { showMainMenu(); };
    gridModeButton_.onClick = [this] { showGridModeMenu(); };
    addTrackButton_.onClick = [this]
    {
        auto& state = controller_.projectManager().state();
        const auto trackName = "Track " + std::to_string(state.tracks.size() + 1);
        controller_.timeline().ensureTrack(state, trackName);
        controller_.projectManager().saveProject();
        refreshScrollableContentSizes();
        timelineView_.repaint();
        trackListView_.repaint();
    };
    newProjectButton_.onClick = [this] { createNewProject(); };
    openProjectButton_.onClick = [this] { openExistingProject(); };
    saveProjectButton_.onClick = [this] { saveCurrentProject(); };
    importAudioButton_.onClick = [this] { importAudioFile(); };
    refreshBackendButton_.onClick = [this] { refreshBackendConnection(); };
    rebuildPreviewButton_.onClick = [this] { rebuildPreviewCache(); };
    zoomOutButton_.onClick = [this] { timelineView_.zoomOut(); };
    zoomResetButton_.onClick = [this] { timelineView_.resetZoom(); };
    zoomInButton_.onClick = [this] { timelineView_.zoomIn(); };
    settingsButton_.onClick = [this] { editSettings(); };
    exportMixButton_.onClick = [this] { exportFullMixFile(); };
    exportRegionButton_.onClick = [this] { exportSelectedRegionFile(); };
    exportStemsButton_.onClick = [this] { exportStemTracksFiles(); };
    startupNoticeLabel_.setJustificationType(juce::Justification::centredLeft);
    startupNoticeLabel_.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(70, 58, 24));
    startupNoticeLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    startupNoticeLabel_.setInterceptsMouseClicks(false, false);
    startupNoticeLabel_.setText(controller_.startupNotice(), juce::dontSendNotification);
    projectStatusLabel_.setJustificationType(juce::Justification::centredRight);
    projectStatusLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(162, 168, 177));
    projectStatusLabel_.setInterceptsMouseClicks(false, false);
    projectStatusLabel_.setVisible(false);
    dismissStartupNoticeButton_.onClick = [this]
    {
        controller_.clearStartupNotice();
        startupNoticeLabel_.setText({}, juce::dontSendNotification);
        resized();
        repaint();
    };
    refreshActionAvailability();
    setWantsKeyboardFocus(true);
    grabKeyboardFocus();
    lastPresentedPlayheadSec_ = controller_.projectManager().state().uiState.playheadSec;
    lastTransportTickMs_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
    appendMainComponentTrace("[bootstrap] MainComponent ctor end");

    initializeAudioDeviceAsync();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(13, 15, 19));
}

MainComponent::~MainComponent()
{
    if (audioSourcePlayer_ != nullptr && audioDeviceInitialized_)
    {
        audioSourcePlayer_->setSource(nullptr);
        audioDeviceManager_.removeAudioCallback(audioSourcePlayer_.get());
    }
}

void MainComponent::initializeAudioDeviceAsync()
{
    if (audioDeviceInitialized_ || audioDeviceInitInProgress_)
    {
        return;
    }

    audioDeviceInitInProgress_ = true;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    audioDeviceInitFuture_ = std::async(std::launch::async, [safeThis]()
    {
        if (safeThis == nullptr)
        {
            return;
        }

        appendMainComponentTrace("[bootstrap] MainComponent deferred audio init begin");
        const auto error = safeThis->audioDeviceManager_.initialise(0, 2, nullptr, true);
        if (!error.isEmpty())
        {
            appendMainComponentTrace("[bootstrap] MainComponent deferred audio init warning: " + error.toStdString());
        }

        juce::MessageManager::callAsync([safeThis]()
        {
            if (safeThis == nullptr)
            {
                return;
            }

            if (!safeThis->audioDeviceInitialized_)
            {
                safeThis->audioDeviceManager_.addAudioCallback(safeThis->audioSourcePlayer_.get());
                safeThis->audioDeviceInitialized_ = true;
            }
            safeThis->audioDeviceInitInProgress_ = false;
            appendMainComponentTrace("[bootstrap] MainComponent deferred audio init end");
        });
    });
}

void MainComponent::refreshScrollableContentSizes()
{
    timelineView_.updateContentSize();

    const auto& state = controller_.projectManager().state();
    const int trackCount = static_cast<int>(state.tracks.size());
    const int trackContentHeight = juce::jmax(
        juce::jmax(trackListViewport_.getHeight(), timelineView_.getHeight()),
        moon::ui::layout::trackContentHeight(trackCount));

    trackListView_.setSize(
        juce::jmax(160, trackListViewport_.getWidth()),
        trackContentHeight);
    lastKnownTrackCount_ = trackCount;
}

void MainComponent::syncTrackStripScroll()
{
    const int timelineY = timelineViewport_.getViewPositionY();
    if (trackListViewport_.getViewPositionY() != timelineY)
    {
        trackListViewport_.setViewPosition(trackListViewport_.getViewPositionX(), timelineY);
    }
    lastTimelineScrollY_ = timelineViewport_.getViewPositionY();
    lastTrackScrollY_ = trackListViewport_.getViewPositionY();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    const auto startupNotice = controller_.startupNotice();
    const bool showStartupNotice = !startupNotice.empty();
    if (showStartupNotice)
    {
        auto noticeArea = area.removeFromTop(56).reduced(4, 2);
        startupNoticeLabel_.setVisible(true);
        dismissStartupNoticeButton_.setVisible(true);
        dismissStartupNoticeButton_.setBounds(noticeArea.removeFromRight(100).reduced(4));
        startupNoticeLabel_.setText(startupNotice, juce::dontSendNotification);
        startupNoticeLabel_.setBounds(noticeArea.reduced(4));
    }
    else
    {
        startupNoticeLabel_.setVisible(false);
        dismissStartupNoticeButton_.setVisible(false);
    }

    auto transport = area.removeFromTop(66);
    auto bottomArea = area.removeFromBottom(aiGenerationPanel_.preferredHeight() + 82);
    auto aiPanelArea = bottomArea.removeFromTop(aiGenerationPanel_.preferredHeight());
    auto taskArea = bottomArea;
    auto inspector = area.removeFromRight(378);
    auto tracks = area.removeFromLeft(198);

    menuButton_.setBounds(transport.removeFromLeft(86).reduced(4));
    auto tuningArea = transport.removeFromRight(560).reduced(4, 8);
    transportBar_.setBounds(transport.reduced(4));
    gridModeButton_.setBounds(tuningArea.removeFromLeft(110).reduced(0, 4));
    zoomValueLabel_.setBounds(tuningArea.removeFromLeft(96));
    fpsValueLabel_.setBounds(tuningArea.removeFromLeft(78));
    waveformDetailLabel_.setBounds(tuningArea.removeFromLeft(74));
    waveformDetailSlider_.setBounds(tuningArea.reduced(0, 4));

    auto trackPanel = tracks.reduced(4);
    auto tracksHeader = trackPanel.removeFromTop(24);
    auto addTrack = trackPanel.removeFromBottom(42);
    tracksHeaderLabel_.setBounds(tracksHeader);
    addTrackButton_.setBounds(addTrack.reduced(4, 2));
    trackListViewport_.setBounds(trackPanel);

    auto timelinePanel = area.reduced(4);
    auto ruler = timelinePanel.removeFromTop(30);
    auto overview = timelinePanel.removeFromBottom(24);
    timelineRulerBar_.setBounds(ruler);
    timelineOverviewBar_.setBounds(overview);
    timelineViewport_.setBounds(timelinePanel);
    aiGenerationPanel_.setBounds(aiPanelArea.reduced(4, 2));

    refreshScrollableContentSizes();
    syncTrackStripScroll();
    lastTimelineScrollY_ = timelineViewport_.getViewPositionY();
    lastTrackScrollY_ = trackListViewport_.getViewPositionY();
    lastTimelinePixelsPerSecond_ = timelineView_.pixelsPerSecond();
    lastTimelineContentWidth_ = timelineView_.getWidth();
    lastTimelineViewportWidth_ = timelineViewport_.getWidth();
    refreshTimelineTuningWidgets();

    inspectorViewport_.setBounds(inspector.reduced(4));
    inspectorPanel_.setSize(juce::jmax(340, inspectorViewport_.getWidth()), 1240);

    taskViewport_.setBounds(taskArea.reduced(4));
    lastTaskPanelContentWidth_ = juce::jmax(320, taskViewport_.getWidth() - 14);
    taskPanel_.updateContentSize(lastTaskPanelContentWidth_);
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".wav"))
        {
            return true;
        }
    }
    return false;
}

void MainComponent::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    if (isInterestedInFileDrag(files))
    {
        updateDropHover(x, y);
    }
}

void MainComponent::fileDragMove(const juce::StringArray& files, int x, int y)
{
    if (isInterestedInFileDrag(files))
    {
        updateDropHover(x, y);
    }
}

void MainComponent::fileDragExit(const juce::StringArray&)
{
    clearDropHover();
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    const auto targetTrackId = resolveDropTrackId(y);
    const auto targetStartSec = resolveDropStartSec(x);
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".wav"))
        {
            const auto logCount = controller_.logger().lineCount();
            if (!controller_.importAudioToTrack(file.toStdString(), targetTrackId, targetStartSec))
            {
                showOperationFailure("Import WAV", "The dropped WAV file could not be imported.", logCount);
            }
        }
    }

    clearDropHover();
    refreshScrollableContentSizes();
    syncTrackStripScroll();
    timelineView_.repaint();
    trackListView_.repaint();
}

void MainComponent::createNewProject()
{
    const auto root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("MoonProject");
    root.createDirectory();

    const auto logCount = controller_.logger().lineCount();
    if (!controller_.createProject("MoonProject", root.getFullPathName().toStdString()))
    {
        showOperationFailure("New Project", "The default MoonProject folder could not be initialized.", logCount);
        return;
    }

    refreshScrollableContentSizes();
    syncTrackStripScroll();
    timelineView_.repaint();
    trackListView_.repaint();
    inspectorPanel_.repaint();
    taskPanel_.repaint();
}

void MainComponent::openExistingProject()
{
    activeFileChooser_ = std::make_unique<juce::FileChooser>(
        "Open Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.json");
    activeFileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto result = chooser.getResult();
            activeFileChooser_.reset();
            if (!result.existsAsFile())
            {
                return;
            }

            const auto logCount = controller_.logger().lineCount();
            if (!controller_.openProject(result.getFullPathName().toStdString()))
            {
                showOperationFailure("Open Project", "Selected project could not be opened.", logCount);
                return;
            }

            refreshScrollableContentSizes();
            syncTrackStripScroll();
            timelineView_.repaint();
            trackListView_.repaint();
            inspectorPanel_.repaint();
            taskPanel_.repaint();
        });
}

void MainComponent::saveCurrentProject()
{
    const auto logCount = controller_.logger().lineCount();
    if (!controller_.saveProject())
    {
        showOperationFailure("Save Project", "Project save failed.", logCount);
        return;
    }
    taskPanel_.repaint();
}

void MainComponent::importAudioFile()
{
    activeFileChooser_ = std::make_unique<juce::FileChooser>(
        "Import WAV",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.wav");
    activeFileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto result = chooser.getResult();
            activeFileChooser_.reset();
            if (!result.existsAsFile())
            {
                return;
            }

            const auto logCount = controller_.logger().lineCount();
            if (!controller_.importAudio(result.getFullPathName().toStdString()))
            {
                showOperationFailure("Import WAV", "The selected WAV file could not be imported.", logCount);
                return;
            }

            refreshScrollableContentSizes();
            syncTrackStripScroll();
            timelineView_.repaint();
            trackListView_.repaint();
        });
}

void MainComponent::exportFullMixFile()
{
    activeFileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Mix",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("mix_export.wav"),
        "*.wav");
    activeFileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser)
        {
            const auto output = chooser.getResult();
            activeFileChooser_.reset();
            if (output == juce::File())
            {
                return;
            }

            const auto logCount = controller_.logger().lineCount();
            if (!controller_.exportFullMix(output.getFullPathName().toStdString()))
            {
                showOperationFailure("Export Mix", "Mix export failed.", logCount);
                return;
            }

            showOperationInfo("Export Mix", "Mix exported to: " + output.getFullPathName());
            taskPanel_.repaint();
        });
}

void MainComponent::exportSelectedRegionFile()
{
    activeFileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Selected Region",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("region_export.wav"),
        "*.wav");
    activeFileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser)
        {
            const auto output = chooser.getResult();
            activeFileChooser_.reset();
            if (output == juce::File())
            {
                return;
            }

            const auto logCount = controller_.logger().lineCount();
            if (!controller_.exportSelectedRegion(output.getFullPathName().toStdString()))
            {
                showOperationFailure("Export Region", "Select a timeline region before exporting.", logCount);
                return;
            }

            showOperationInfo("Export Region", "Region exported to: " + output.getFullPathName());
            taskPanel_.repaint();
        });
}

void MainComponent::exportStemTracksFiles()
{
    activeFileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Stems Folder",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MoonStemExports"));
    activeFileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& chooser)
        {
            const auto outputDir = chooser.getResult();
            activeFileChooser_.reset();
            if (outputDir == juce::File())
            {
                return;
            }

            outputDir.createDirectory();
            const auto logCount = controller_.logger().lineCount();
            if (!controller_.exportStemTracks(outputDir.getFullPathName().toStdString()))
            {
                showOperationFailure("Export Stems", "No stem tracks are available to export.", logCount);
                return;
            }

            showOperationInfo("Export Stems", "Stem WAV files exported to: " + outputDir.getFullPathName());
            taskPanel_.repaint();
        });
}

void MainComponent::refreshBackendConnection()
{
    const auto logCount = controller_.logger().lineCount();
    if (controller_.refreshBackendStatus())
    {
        refreshAIGenerationPanel();
        resized();
        transportBar_.refresh();
        showOperationInfo("Refresh Backend", "Backend connection refreshed successfully.");
        return;
    }

    refreshAIGenerationPanel();
    resized();
    transportBar_.refresh();
    showOperationFailure(
        "Refresh Backend",
        "Backend is still unavailable. The app will continue using stub fallback jobs.",
        logCount);
}

void MainComponent::rebuildPreviewCache()
{
    const auto logCount = controller_.logger().lineCount();
    if (!controller_.rebuildPreviewPlayback())
    {
        showOperationFailure(
            "Rebuild Preview",
            "Preview cache could not be rebuilt. Import audio first or check the task/log panel for details.",
            logCount);
        return;
    }

    transportBar_.refresh();
    timelineView_.repaint();
    taskPanel_.repaint();
    showOperationInfo("Rebuild Preview", "Timeline preview cache rebuilt successfully.");
}

void MainComponent::editSettings()
{
    showOperationInfo(
        "App Settings",
        "Settings editing is temporarily disabled in this JUCE compatibility build.");
}

void MainComponent::showMainMenu()
{
    refreshActionAvailability();

    juce::PopupMenu menu;
    menu.addSectionHeader("File");
    menu.addItem(1, "New Project");
    menu.addItem(2, "Open Project...");
    menu.addItem(3, "Save Project");
    menu.addItem(4, "Import WAV...");

    menu.addSectionHeader("View");
    menu.addItem(10, "Zoom Out");
    menu.addItem(11, "Zoom Reset");
    menu.addItem(12, "Zoom In");

    menu.addSectionHeader("Backend");
    menu.addItem(20, "Refresh Backend");
    menu.addItem(21, "Rebuild Preview", rebuildPreviewButton_.isEnabled());

    menu.addSectionHeader("Export");
    menu.addItem(30, "Export Mix...", exportMixButton_.isEnabled());
    menu.addItem(31, "Export Region...", exportRegionButton_.isEnabled());
    menu.addItem(32, "Export Stems...", exportStemsButton_.isEnabled());

    menu.addSectionHeader("App");
    menu.addItem(40, "Settings");

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(menuButton_),
        [this](int result)
        {
            switch (result)
            {
            case 1: createNewProject(); break;
            case 2: openExistingProject(); break;
            case 3: saveCurrentProject(); break;
            case 4: importAudioFile(); break;
            case 10: timelineView_.zoomOut(); break;
            case 11: timelineView_.resetZoom(); break;
            case 12: timelineView_.zoomIn(); break;
            case 20: refreshBackendConnection(); break;
            case 21: rebuildPreviewCache(); break;
            case 30: exportFullMixFile(); break;
            case 31: exportSelectedRegionFile(); break;
            case 32: exportStemTracksFiles(); break;
            case 40: editSettings(); break;
            default: break;
            }
        });
}

void MainComponent::refreshActionAvailability()
{
    const auto& state = controller_.projectManager().state();
    const bool hasClips = !state.clips.empty();
    const bool hasRegion = state.uiState.hasSelectedRegion;

    bool hasStemTracks = false;
    for (const auto& track : state.tracks)
    {
        if (track.name == "Vocals" || track.name == "Drums" || track.name == "Bass" || track.name == "Other"
            || track.name == "vocals" || track.name == "drums" || track.name == "bass" || track.name == "other")
        {
            hasStemTracks = true;
            break;
        }
    }

    exportMixButton_.setEnabled(hasClips);
    exportRegionButton_.setEnabled(hasRegion);
    exportStemsButton_.setEnabled(hasStemTracks);
    rebuildPreviewButton_.setEnabled(hasClips);
}

void MainComponent::refreshProjectStatusLabel()
{
    const auto& state = controller_.projectManager().state();
    juce::String text = "Project: ";
    text += state.projectName.empty() ? "Untitled" : juce::String(state.projectName);
    text += " | ";
    text += juce::String(state.sampleRate);
    text += " Hz";
    text += " | ";
    text += juce::String(static_cast<int>(state.tracks.size()));
    text += " tracks";
    text += " | ";
    text += juce::String(static_cast<int>(state.clips.size()));
    text += " clips";
    text += " | ";
    text += juce::String(controller_.transport().playbackRouteSummary());

    if (const auto projectFile = controller_.projectFilePath(); projectFile.has_value())
    {
        text += " | ";
        text += juce::File(*projectFile).getFileName();
    }

    projectStatusLabel_.setText(text, juce::dontSendNotification);
}

void MainComponent::showOperationFailure(const juce::String& title, const juce::String& fallbackMessage, std::size_t previousLogCount)
{
    juce::String details = fallbackMessage;
    if (const auto latestError = controller_.logger().latestErrorSince(previousLogCount); latestError.has_value())
    {
        details = latestError->c_str();
    }

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        title,
        details);
}

void MainComponent::showOperationInfo(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        title,
        message);
}

void MainComponent::refreshTimelineTuningWidgets()
{
    const auto zoomPercent = static_cast<int>(std::round((timelineView_.pixelsPerSecond() / 100.0) * 100.0));
    gridModeButton_.setButtonText(gridModeLabel(timelineView_.gridMode()));
    zoomValueLabel_.setText("Zoom " + juce::String(zoomPercent) + "%", juce::dontSendNotification);
    const auto detailPercent = static_cast<int>(std::round(waveformDetailSlider_.getValue()));
    waveformDetailLabel_.setText("Wave " + juce::String(detailPercent) + "%", juce::dontSendNotification);
}

void MainComponent::showGridModeMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "(none)");
    menu.addSeparator();
    menu.addItem(2, "1/6 step");
    menu.addItem(3, "1/4 step");
    menu.addItem(4, "1/3 step");
    menu.addItem(5, "1/2 step");
    menu.addItem(6, "Step");
    menu.addSeparator();
    menu.addItem(7, "1/6 beat");
    menu.addItem(8, "1/4 beat");
    menu.addItem(9, "1/3 beat");
    menu.addItem(10, "1/2 beat");
    menu.addItem(11, "Beat");
    menu.addItem(12, "Bar");
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&gridModeButton_),
        [this](int result)
        {
            moon::ui::TimelineGridMode mode = timelineView_.gridMode();
            switch (result)
            {
            case 1: mode = moon::ui::TimelineGridMode::None; break;
            case 2: mode = moon::ui::TimelineGridMode::StepDiv6; break;
            case 3: mode = moon::ui::TimelineGridMode::StepDiv4; break;
            case 4: mode = moon::ui::TimelineGridMode::StepDiv3; break;
            case 5: mode = moon::ui::TimelineGridMode::StepDiv2; break;
            case 6: mode = moon::ui::TimelineGridMode::Step; break;
            case 7: mode = moon::ui::TimelineGridMode::BeatDiv6; break;
            case 8: mode = moon::ui::TimelineGridMode::BeatDiv4; break;
            case 9: mode = moon::ui::TimelineGridMode::BeatDiv3; break;
            case 10: mode = moon::ui::TimelineGridMode::BeatDiv2; break;
            case 11: mode = moon::ui::TimelineGridMode::Beat; break;
            case 12: mode = moon::ui::TimelineGridMode::Bar; break;
            default: return;
            }

            timelineView_.setGridMode(mode);
            timelineRulerBar_.repaint();
            timelineOverviewBar_.repaint();
            refreshTimelineTuningWidgets();
        });
}

std::string MainComponent::resolveDropTrackId(int y) const
{
    auto& state = controller_.projectManager().state();
    if (state.tracks.empty())
    {
        return {};
    }

    const auto timelineArea = timelineViewport_.getBounds();
    const auto trackArea = trackListViewport_.getBounds();
    int contentY = 0;

    if (timelineArea.contains(timelineArea.getCentreX(), y))
    {
        contentY = y - timelineArea.getY() + timelineViewport_.getViewPositionY();
    }
    else if (trackArea.contains(trackArea.getCentreX(), y))
    {
        contentY = y - trackArea.getY() + trackListViewport_.getViewPositionY();
    }
    else
    {
        contentY = timelineViewport_.getViewPositionY();
    }

    // Clamp to the nearest valid row instead of canceling the drop; this matches the
    // user's likely intent when they miss the lane by a few pixels.
    auto resolvedTrackId = timelineView_.nearestTrackIdForY(contentY);
    if (resolvedTrackId.empty())
    {
        resolvedTrackId = state.tracks.front().id;
    }
    return resolvedTrackId;
}

double MainComponent::resolveDropStartSec(int x) const
{
    const auto timelineArea = timelineViewport_.getBounds();
    if (!timelineArea.contains(x, timelineArea.getCentreY()))
    {
        return 0.0;
    }

    const auto contentX = x - timelineArea.getX() + timelineViewport_.getViewPositionX();
    return timelineView_.timeForContentX(contentX);
}

void MainComponent::updateDropHover(int x, int y)
{
    juce::ignoreUnused(x);
    const auto trackId = resolveDropTrackId(y);
    timelineView_.setDropHoverTrackId(trackId);
    trackListView_.setDropHoverTrackId(trackId);
}

void MainComponent::clearDropHover()
{
    timelineView_.setDropHoverTrackId(std::string{});
    trackListView_.setDropHoverTrackId(std::string{});
}

void MainComponent::refreshFpsCounter(double deltaSec)
{
    if (deltaSec <= 0.0)
    {
        return;
    }

    const auto currentFps = juce::jlimit(1.0, 240.0, 1.0 / deltaSec);
    fpsSmoothed_ = fpsSmoothed_ * 0.88 + currentFps * 0.12;
    fpsValueLabel_.setText("FPS " + juce::String(static_cast<int>(std::round(fpsSmoothed_))), juce::dontSendNotification);
}

void MainComponent::repaintPlayheadPresentation(double previousPlayheadSec, double nextPlayheadSec, bool forceFull)
{
    repaintSeekViews(timelineView_, timelineRulerBar_, previousPlayheadSec, nextPlayheadSec, forceFull);
    lastPresentedPlayheadSec_ = nextPlayheadSec;
}

void MainComponent::refreshAIGenerationPanel()
{
    refreshAIGenerationPanelModelState();
    refreshAIGenerationPanelTaskState();
}

void MainComponent::refreshAIGenerationPanelModelState()
{
    aiGenerationPanel_.setModelRegistrySnapshot(controller_.modelRegistrySnapshot());
}

void MainComponent::refreshAIGenerationPanelTaskState()
{
    aiGenerationPanel_.refreshTaskState(controller_.tasks());
}

void MainComponent::openModelManagerForCapability(moon::engine::ModelCapability capability)
{
    auto* panel = new moon::ui::ModelManagerPanel();
    panel->setPreferredCapability(capability);
    panel->setSnapshot(controller_.modelRegistrySnapshot());
    panel->setRefreshCallback(
        [this, panel](std::string& errorMessage)
        {
            if (!controller_.refreshModelRegistry(&errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setPollCallback(
        [this, panel](std::string& errorMessage)
        {
            const bool changed = controller_.pollModelOperations(&errorMessage);
            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return changed;
        });
    panel->setSyncRemoteCatalogCallback(
        [this, panel](std::string& errorMessage)
        {
            if (!controller_.syncRemoteModelCatalog(&errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setSetActiveCallback(
        [this, panel](moon::engine::ModelCapability bindingCapability, const std::string& modelId, std::string& errorMessage)
        {
            if (!controller_.setActiveGenerationModel(bindingCapability, modelId, errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setDownloadCallback(
        [this, panel](const std::string& modelId, std::string& errorMessage)
        {
            if (!controller_.downloadModel(modelId, errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setUpdateCallback(
        [this, panel](const std::string& modelId, std::string& errorMessage)
        {
            if (!controller_.updateModel(modelId, errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setVerifyCallback(
        [this, panel](const std::string& modelId, std::string& errorMessage)
        {
            if (!controller_.verifyInstalledModel(modelId, errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setRemoveCallback(
        [this, panel](const std::string& modelId, std::string& errorMessage)
        {
            if (!controller_.removeInstalledModel(modelId, errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setAddExistingCallback(
        [this, panel](const std::string& modelId, const std::string& folderPath, std::string& errorMessage)
        {
            if (!controller_.addExistingModelFolder(modelId, folderPath, errorMessage))
            {
                return false;
            }

            panel->setSnapshot(controller_.modelRegistrySnapshot());
            refreshAIGenerationPanel();
            return true;
        });
    panel->setOpenModelsFolderCallback(
        [this]()
        {
            juce::File(controller_.modelsRootDirectory().string()).revealToUser();
        });
    panel->setCloseCallback(
        [this]()
        {
            controller_.cancelAllModelOperations();
            controller_.cancelGenerationRuntimePreparation();
            refreshAIGenerationPanel();
        });

    juce::DialogWindow::LaunchOptions options;
    panel->setSize(980, 540);
    options.content.setOwned(panel);
    options.dialogTitle = "Model Manager";
    options.dialogBackgroundColour = juce::Colour::fromRGB(13, 16, 21);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void MainComponent::timerCallback()
{
    if (startupNoticeLabel_.isVisible() != !controller_.startupNotice().empty())
    {
        resized();
    }

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto deltaSec = juce::jlimit(1.0 / 240.0, 0.05, (nowMs - lastTransportTickMs_) / 1000.0);
    lastTransportTickMs_ = nowMs;
    refreshFpsCounter(deltaSec);

    const auto previousPlayheadSec = lastPresentedPlayheadSec_;
    controller_.transport().tick(deltaSec);
    controller_.refreshPlaybackUiState();
    const auto nextPlayheadSec = controller_.projectManager().state().uiState.playheadSec;
    controller_.maintainPreviewPlayback();
    ++modelPollTickCounter_;
    const bool shouldPollModels = modelPollTickCounter_ >= 24;
    if (shouldPollModels)
    {
        modelPollTickCounter_ = 0;
        if (controller_.pollModelOperations())
        {
            refreshAIGenerationPanel();
        }
    }
    ++taskPollTickCounter_;
    ++autosaveTickCounter_;

    const auto activeTaskCount = controller_.tasks().activeTaskCount();
    const int taskPollThreshold = activeTaskCount > 0 ? 8 : 30;
    const bool shouldPollTasks = taskPollTickCounter_ >= taskPollThreshold;
    bool taskStateChanged = false;
    if (shouldPollTasks)
    {
        taskPollTickCounter_ = 0;
        taskStateChanged = controller_.pollTasks();
        if (taskStateChanged)
        {
            refreshAIGenerationPanelTaskState();
        }
    }

    if (autosaveTickCounter_ >= 600)
    {
        autosaveTickCounter_ = 0;
        controller_.autosaveIfNeeded();
    }

    ++transportRefreshTickCounter_;
    const bool shouldRefreshTransportBar = controller_.transport().isPlaying() || transportRefreshTickCounter_ >= 3;
    if (shouldRefreshTransportBar)
    {
        transportRefreshTickCounter_ = 0;
        transportBar_.refresh();
    }
    const int previousScrollX = timelineViewport_.getViewPositionX();
    const auto& state = controller_.projectManager().state();
    const int currentTrackCount = static_cast<int>(state.tracks.size());
    if (currentTrackCount != lastKnownTrackCount_)
    {
        refreshScrollableContentSizes();
    }
    const auto currentPixelsPerSecond = timelineView_.pixelsPerSecond();
    const int currentContentWidth = timelineView_.getWidth();
    const int currentViewportWidth = timelineViewport_.getWidth();
    if (!syncingTrackScroll_)
    {
        const int timelineY = timelineViewport_.getViewPositionY();
        const int trackY = trackListViewport_.getViewPositionY();

        if (timelineY != trackY)
        {
            syncingTrackScroll_ = true;
            if (timelineY != lastTimelineScrollY_)
            {
                trackListViewport_.setViewPosition(trackListViewport_.getViewPositionX(), timelineY);
            }
            else if (trackY != lastTrackScrollY_)
            {
                timelineViewport_.setViewPosition(timelineViewport_.getViewPositionX(), trackY);
            }
            syncingTrackScroll_ = false;
        }

        lastTimelineScrollY_ = timelineViewport_.getViewPositionY();
        lastTrackScrollY_ = trackListViewport_.getViewPositionY();
    }
    repaintPlayheadPresentation(previousPlayheadSec, nextPlayheadSec, false);
    if (timelineViewport_.getViewPositionX() != previousScrollX)
    {
        timelineOverviewBar_.repaintViewportDelta(previousScrollX, timelineViewport_.getViewPositionX(), timelineView_.getWidth(), timelineViewport_.getWidth());
        timelineRulerBar_.repaint();
    }

    if (std::abs(currentPixelsPerSecond - lastTimelinePixelsPerSecond_) > 0.0001
        || currentContentWidth != lastTimelineContentWidth_
        || currentViewportWidth != lastTimelineViewportWidth_)
    {
        timelineRulerBar_.repaint();
        timelineOverviewBar_.repaint();
        timelineView_.repaint();
        lastTimelinePixelsPerSecond_ = currentPixelsPerSecond;
        lastTimelineContentWidth_ = currentContentWidth;
        lastTimelineViewportWidth_ = currentViewportWidth;
    }

    if (shouldPollTasks)
    {
        const auto currentTaskPanelWidth = juce::jmax(320, taskViewport_.getWidth() - 14);
        if (currentTaskPanelWidth != lastTaskPanelContentWidth_)
        {
            lastTaskPanelContentWidth_ = currentTaskPanelWidth;
            taskPanel_.updateContentSize(lastTaskPanelContentWidth_);
        }
        refreshActionAvailability();
        refreshProjectStatusLabel();
        if (taskStateChanged || controller_.tasks().activeTaskCount() > 0)
        {
            taskPanel_.repaint();
        }
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();

    if (key == juce::KeyPress::spaceKey)
    {
        if (controller_.transport().isPlaying())
        {
            controller_.pauseTransport();
        }
        else
        {
            controller_.playTransport();
        }
        transportBar_.refresh();
        return true;
    }

    if (modifiers.isCommandDown() || modifiers.isCtrlDown())
    {
        const auto text = key.getTextCharacter();
        if (text == 'n' || text == 'N')
        {
            createNewProject();
            return true;
        }
        if (text == 'o' || text == 'O')
        {
            openExistingProject();
            return true;
        }
        if (text == 's' || text == 'S')
        {
            saveCurrentProject();
            return true;
        }
        if (text == 'i' || text == 'I')
        {
            importAudioFile();
            return true;
        }
        if (text == 'd' || text == 'D')
        {
            controller_.duplicateSelectedClip();
            timelineView_.repaint();
            inspectorPanel_.repaint();
            taskPanel_.repaint();
            return true;
        }
        if (text == 'e' || text == 'E')
        {
            controller_.splitSelectedClipAtPlayhead();
            timelineView_.repaint();
            inspectorPanel_.repaint();
            taskPanel_.repaint();
            return true;
        }
        if (text == 't' || text == 'T')
        {
            controller_.activateSelectedTake();
            timelineView_.repaint();
            inspectorPanel_.repaint();
            taskPanel_.repaint();
            return true;
        }
        if (text == '=' || text == '+')
        {
            timelineView_.zoomIn();
            return true;
        }
        if (text == '-' || text == '_')
        {
            timelineView_.zoomOut();
            return true;
        }
        if (text == '0')
        {
            timelineView_.resetZoom();
            return true;
        }
    }

    if (key == juce::KeyPress::leftKey)
    {
        controller_.nudgeTimelinePlayhead(modifiers.isShiftDown() ? -1.0 : -0.1);
        timelineView_.repaint();
        transportBar_.refresh();
        return true;
    }

    if (key == juce::KeyPress::rightKey)
    {
        controller_.nudgeTimelinePlayhead(modifiers.isShiftDown() ? 1.0 : 0.1);
        timelineView_.repaint();
        transportBar_.refresh();
        return true;
    }

    if (key == juce::KeyPress::homeKey)
    {
        controller_.seekTimelinePlayhead(0.0);
        timelineView_.repaint();
        transportBar_.refresh();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        controller_.clearSelectedRegion();
        timelineView_.repaint();
        inspectorPanel_.repaint();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        auto& state = controller_.projectManager().state();
        bool changed = false;
        if (!state.uiState.selectedClipId.empty())
        {
            changed = controller_.deleteSelectedClip();
        }
        else if (!state.uiState.selectedTrackId.empty())
        {
            changed = controller_.deleteTrack(state.uiState.selectedTrackId);
            if (changed)
            {
                refreshScrollableContentSizes();
                syncTrackStripScroll();
                trackListView_.repaint();
            }
        }

        if (changed)
        {
            timelineView_.repaint();
            timelineRulerBar_.repaint();
            timelineOverviewBar_.repaint();
            inspectorPanel_.repaint();
            taskPanel_.repaint();
        }
        return true;
    }

    return false;
}
}
#endif





