#include "TimelineView.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
namespace
{
constexpr int kTimelineLeftPadding = 200;
constexpr double kPixelsPerSecond = 100.0;
constexpr int kHeaderHeight = 24;
constexpr int kRulerHeight = 20;

std::string resolveAssetPath(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip)
{
    if (const auto sourceIt = state.sourceAssets.find(clip.assetId); sourceIt != state.sourceAssets.end())
    {
        return sourceIt->second.path;
    }
    if (const auto generatedIt = state.generatedAssets.find(clip.assetId); generatedIt != state.generatedAssets.end())
    {
        return generatedIt->second.path;
    }
    return {};
}

bool anyTrackSoloed(const moon::engine::ProjectState& state)
{
    return std::any_of(
        state.tracks.begin(),
        state.tracks.end(),
        [](const moon::engine::TrackInfo& track)
        {
            return track.solo;
        });
}

bool trackIsAudible(const moon::engine::ProjectState& state, const std::string& trackId)
{
    const auto trackIt = std::find_if(
        state.tracks.begin(),
        state.tracks.end(),
        [&trackId](const moon::engine::TrackInfo& track)
        {
            return track.id == trackId;
        });
    if (trackIt == state.tracks.end())
    {
        return true;
    }

    if (anyTrackSoloed(state))
    {
        return trackIt->solo;
    }

    return !trackIt->mute;
}

bool clipParticipatesInMix(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip)
{
    return clip.activeTake && trackIsAudible(state, clip.trackId);
}

double clipEndSec(const moon::engine::ClipInfo& clip)
{
    return clip.startSec + clip.durationSec;
}
}

TimelineView::TimelineView(moon::engine::TimelineFacade& timeline,
                           moon::engine::ProjectManager& projectManager,
                           moon::engine::WaveformService& waveformService,
                           moon::engine::TransportFacade& transport,
                           std::function<void(double)> seekTimelineCallback,
                           std::function<void()> beginClipDragCallback,
                           std::function<void(bool)> endClipDragCallback)
    : timeline_(timeline)
    , projectManager_(projectManager)
    , waveformService_(waveformService)
    , transport_(transport)
    , seekTimelineCallback_(std::move(seekTimelineCallback))
    , beginClipDragCallback_(std::move(beginClipDragCallback))
    , endClipDragCallback_(std::move(endClipDragCallback))
{
}

void TimelineView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::darkgrey);
    g.drawRect(getLocalBounds(), 1);
    g.setColour(juce::Colours::white);
    g.drawText("Timeline", getLocalBounds().removeFromTop(kHeaderHeight), juce::Justification::centredLeft);

    const auto& state = projectManager_.state();
    g.setColour(juce::Colours::darkslategrey);
    g.fillRect(kTimelineLeftPadding, kHeaderHeight, getWidth() - kTimelineLeftPadding, kRulerHeight);
    g.setColour(juce::Colours::lightgrey);
    for (int second = 0; second < juce::jmax(1, static_cast<int>((getWidth() - kTimelineLeftPadding) / kPixelsPerSecond) + 1); ++second)
    {
        const auto x = kTimelineLeftPadding + static_cast<int>(second * kPixelsPerSecond);
        g.drawLine(static_cast<float>(x), static_cast<float>(kHeaderHeight), static_cast<float>(x), static_cast<float>(kHeaderHeight + kRulerHeight), 1.0f);
        g.drawText(juce::String(second) + "s", x + 3, kHeaderHeight + 2, 40, 14, juce::Justification::centredLeft);
    }

    int y = 40;
    for (std::size_t trackIndex = 0; trackIndex < state.tracks.size(); ++trackIndex)
    {
        const auto& track = state.tracks[trackIndex];
        const auto rowY = clipYForIndex(static_cast<int>(trackIndex)) - 10;
        g.setColour(juce::Colours::dimgrey);
        g.drawText(track.name, 8, rowY, 180, 24, juce::Justification::centredLeft);
        g.setColour(track.id == state.uiState.selectedTrackId ? juce::Colours::darkorange : juce::Colours::darkslategrey);
        g.drawLine(190.0f, static_cast<float>(rowY + 30), static_cast<float>(getWidth()), static_cast<float>(rowY + 30), 1.0f);
        y += 80;
    }

    for (const auto& clip : state.clips)
    {
        int trackIndex = 0;
        for (std::size_t i = 0; i < state.tracks.size(); ++i)
        {
            if (state.tracks[i].id == clip.trackId)
            {
                trackIndex = static_cast<int>(i);
                break;
            }
        }

        const auto bounds = clipBounds(state, clip);
        const int x = bounds.getX();
        const int width = bounds.getWidth();
        const int clipY = clipYForIndex(trackIndex);
        const auto baseColour = clip.selected
            ? juce::Colours::orange
            : (clip.activeTake ? juce::Colours::steelblue : juce::Colours::slategrey);
        g.setColour(baseColour);
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(clipY), static_cast<float>(width), 48.0f, 6.0f);
        if (clip.fadeInSec > 0.0)
        {
            const auto fadeWidth = juce::jmin(width / 2, static_cast<int>(clip.fadeInSec * kPixelsPerSecond));
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawLine(static_cast<float>(x), static_cast<float>(clipY + 48), static_cast<float>(x + fadeWidth), static_cast<float>(clipY), 2.0f);
        }
        if (clip.fadeOutSec > 0.0)
        {
            const auto fadeWidth = juce::jmin(width / 2, static_cast<int>(clip.fadeOutSec * kPixelsPerSecond));
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawLine(static_cast<float>(x + width - fadeWidth), static_cast<float>(clipY), static_cast<float>(x + width), static_cast<float>(clipY + 48), 2.0f);
        }
        g.setColour(juce::Colours::white);
        g.drawText(clip.id, x + 8, clipY + 8, width - 16, 24, juce::Justification::centredLeft);
        if (!clip.takeGroupId.empty())
        {
            g.setColour(clip.activeTake ? juce::Colours::lightgreen : juce::Colours::lightgrey);
            g.drawText(clip.activeTake ? "ACTIVE TAKE" : "ALT TAKE", x + 8, clipY + 26, width - 16, 16, juce::Justification::centredLeft);
        }

        const auto assetPath = resolveAssetPath(state, clip);
        const auto waveform = waveformService_.requestWaveform(assetPath);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        const auto innerWidth = juce::jmax(1, width - 12);
        const auto peakCount = juce::jmax(1, static_cast<int>(waveform.peaks.size()));
        for (int i = 0; i < innerWidth; ++i)
        {
            const float centerY = static_cast<float>(clipY + 24);
            const auto peakIndex = std::min(peakCount - 1, (i * peakCount) / innerWidth);
            const float amplitude = waveform.peaks.empty() ? 4.0f : waveform.peaks[static_cast<std::size_t>(peakIndex)] * 18.0f;
            g.drawLine(static_cast<float>(x + 6 + i), centerY - amplitude, static_cast<float>(x + 6 + i), centerY + amplitude, 1.0f);
        }
    }

    for (std::size_t leftIndex = 0; leftIndex < state.clips.size(); ++leftIndex)
    {
        const auto& leftClip = state.clips[leftIndex];
        if (!clipParticipatesInMix(state, leftClip))
        {
            continue;
        }

        for (std::size_t rightIndex = leftIndex + 1; rightIndex < state.clips.size(); ++rightIndex)
        {
            const auto& rightClip = state.clips[rightIndex];
            if (!clipParticipatesInMix(state, rightClip) || leftClip.trackId != rightClip.trackId)
            {
                continue;
            }

            const auto overlapStart = std::max(leftClip.startSec, rightClip.startSec);
            const auto overlapEnd = std::min(clipEndSec(leftClip), clipEndSec(rightClip));
            if (overlapEnd <= overlapStart)
            {
                continue;
            }

            int trackIndex = 0;
            for (std::size_t i = 0; i < state.tracks.size(); ++i)
            {
                if (state.tracks[i].id == leftClip.trackId)
                {
                    trackIndex = static_cast<int>(i);
                    break;
                }
            }

            const auto overlapX = kTimelineLeftPadding + static_cast<int>(overlapStart * kPixelsPerSecond);
            const auto overlapW = juce::jmax(2, static_cast<int>((overlapEnd - overlapStart) * kPixelsPerSecond));
            const auto clipY = clipYForIndex(trackIndex);
            const auto highlightColour = (leftClip.selected || rightClip.selected)
                ? juce::Colours::gold.withAlpha(0.35f)
                : juce::Colours::white.withAlpha(0.18f);
            g.setColour(highlightColour);
            g.fillRoundedRectangle(static_cast<float>(overlapX), static_cast<float>(clipY + 14), static_cast<float>(overlapW), 20.0f, 4.0f);
            g.setColour(highlightColour.brighter(0.3f));
            g.drawRoundedRectangle(static_cast<float>(overlapX), static_cast<float>(clipY + 14), static_cast<float>(overlapW), 20.0f, 4.0f, 1.0f);
            g.drawText("XF", overlapX + 4, clipY + 15, juce::jmax(18, overlapW - 8), 18, juce::Justification::centredLeft);
        }
    }

    if (state.uiState.hasSelectedRegion)
    {
        const auto regionX = kTimelineLeftPadding + static_cast<int>(state.uiState.selectedRegionStartSec * kPixelsPerSecond);
        const auto regionW = juce::jmax(2, static_cast<int>((state.uiState.selectedRegionEndSec - state.uiState.selectedRegionStartSec) * kPixelsPerSecond));
        g.setColour(juce::Colours::yellow.withAlpha(0.18f));
        g.fillRect(regionX, 28, regionW, getHeight() - 36);
        g.setColour(juce::Colours::yellow);
        g.drawRect(regionX, 28, regionW, getHeight() - 36, 1);
    }

    const auto playheadTimelineSec = state.uiState.selectedClipId.empty()
        ? state.uiState.playheadSec
        : state.uiState.playheadSec;
    const auto playheadX = kTimelineLeftPadding + static_cast<int>(playheadTimelineSec * kPixelsPerSecond);
    g.setColour(transport_.isPlaying() ? juce::Colours::red : juce::Colours::orangered);
    g.drawLine(static_cast<float>(playheadX), static_cast<float>(kHeaderHeight), static_cast<float>(playheadX), static_cast<float>(getHeight()), 2.0f);
}

void TimelineView::mouseDown(const juce::MouseEvent& event)
{
    auto& state = projectManager_.state();
    draggingPlayhead_ = false;
    draggingRegion_ = false;
    draggingClip_ = false;
    clipMovedDuringDrag_ = false;
    draggedClipId_.clear();

    if (isRulerHit(event.getPosition()))
    {
        draggingPlayhead_ = true;
        if (seekTimelineCallback_)
        {
            seekTimelineCallback_(xToTime(event.x));
        }
        repaint();
        return;
    }

    for (std::size_t i = 0; i < state.clips.size(); ++i)
    {
        const auto& clip = state.clips[i];
        const auto clipArea = clipBounds(state, clip);

        if (clipArea.contains(event.getPosition()))
        {
            timeline_.selectClip(state, clip.id);
            timeline_.selectTrack(state, clip.trackId);
            if (beginClipDragCallback_)
            {
                beginClipDragCallback_();
            }
            draggingClip_ = true;
            draggedClipId_ = clip.id;
            clipDragOffsetSec_ = xToTime(event.x) - clip.startSec;
            repaint();
            return;
        }
    }

    draggingRegion_ = true;
    dragStartSec_ = xToTime(event.x);
    timeline_.setSelectedRegion(state, dragStartSec_, dragStartSec_);
    repaint();
}

void TimelineView::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingPlayhead_)
    {
        if (seekTimelineCallback_)
        {
            seekTimelineCallback_(xToTime(event.x));
        }
        repaint();
        return;
    }

    if (draggingClip_)
    {
        auto& state = projectManager_.state();
        const auto moved = timeline_.moveClip(state, draggedClipId_, xToTime(event.x) - clipDragOffsetSec_);
        clipMovedDuringDrag_ = clipMovedDuringDrag_ || moved;
        repaint();
        return;
    }

    if (!draggingRegion_)
    {
        return;
    }

    timeline_.setSelectedRegion(projectManager_.state(), dragStartSec_, xToTime(event.x));
    repaint();
}

void TimelineView::mouseUp(const juce::MouseEvent& event)
{
    if (draggingPlayhead_)
    {
        draggingPlayhead_ = false;
        if (seekTimelineCallback_)
        {
            seekTimelineCallback_(xToTime(event.x));
        }
        repaint();
        return;
    }

    if (draggingClip_)
    {
        draggingClip_ = false;
        if (endClipDragCallback_)
        {
            endClipDragCallback_(clipMovedDuringDrag_);
        }
        else if (clipMovedDuringDrag_)
        {
            projectManager_.saveProject();
        }
        repaint();
        return;
    }

    if (!draggingRegion_)
    {
        return;
    }

    draggingRegion_ = false;
    timeline_.setSelectedRegion(projectManager_.state(), dragStartSec_, xToTime(event.x));
    repaint();
}

double TimelineView::xToTime(int x) const
{
    return juce::jmax(0.0, (static_cast<double>(x) - static_cast<double>(kTimelineLeftPadding)) / kPixelsPerSecond);
}

bool TimelineView::isRulerHit(const juce::Point<int>& point) const
{
    return point.y >= kHeaderHeight && point.y <= (kHeaderHeight + kRulerHeight) && point.x >= kTimelineLeftPadding;
}

int TimelineView::clipYForIndex(int index) const
{
    return 50 + index * 80;
}

juce::Rectangle<int> TimelineView::clipBounds(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip) const
{
    int trackIndex = 0;
    for (std::size_t i = 0; i < state.tracks.size(); ++i)
    {
        if (state.tracks[i].id == clip.trackId)
        {
            trackIndex = static_cast<int>(i);
            break;
        }
    }

    return juce::Rectangle<int>(
        kTimelineLeftPadding + static_cast<int>(clip.startSec * kPixelsPerSecond),
        clipYForIndex(trackIndex),
        juce::jmax(120, static_cast<int>(clip.durationSec * kPixelsPerSecond)),
        48);
}
}
#endif
