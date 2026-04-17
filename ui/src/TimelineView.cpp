#include "TimelineView.h"

#include <algorithm>
#include <cmath>

#if MOON_HAS_JUCE
namespace moon::ui
{
namespace
{
constexpr int kTimelineLeftPadding = 0;
constexpr int kHeaderHeight = 0;
constexpr int kRulerHeight = 30;
constexpr int kFooterHeight = 26;

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
                           std::function<void(bool)> endClipDragCallback,
                           std::function<void()> splitClipCallback,
                           std::function<void()> deleteClipCallback,
                           std::function<bool(double)> setFadeInCallback,
                           std::function<bool(double)> setFadeOutCallback,
                           std::function<bool(double)> trimLeftCallback,
                           std::function<bool(double)> trimRightCallback)
    : timeline_(timeline)
    , projectManager_(projectManager)
    , waveformService_(waveformService)
    , transport_(transport)
    , seekTimelineCallback_(std::move(seekTimelineCallback))
    , beginClipDragCallback_(std::move(beginClipDragCallback))
    , endClipDragCallback_(std::move(endClipDragCallback))
    , splitClipCallback_(std::move(splitClipCallback))
    , deleteClipCallback_(std::move(deleteClipCallback))
    , setFadeInCallback_(std::move(setFadeInCallback))
    , setFadeOutCallback_(std::move(setFadeOutCallback))
    , trimLeftCallback_(std::move(trimLeftCallback))
    , trimRightCallback_(std::move(trimRightCallback))
{
}

void TimelineView::resized()
{
    updateContentSize();
}

void TimelineView::paint(juce::Graphics& g)
{
    const auto& state = projectManager_.state();
    const auto tempo = juce::jmax(30.0, state.tempo);
    const double beatSec = 60.0 / tempo;
    const double barSec = beatSec * 4.0;

    g.fillAll(juce::Colour::fromRGB(18, 20, 24));
    g.setColour(juce::Colour::fromRGB(28, 31, 36));
    g.fillRect(kTimelineLeftPadding, 0, getWidth() - kTimelineLeftPadding, kHeaderHeight + kRulerHeight);
    g.setColour(juce::Colour::fromRGB(12, 14, 18));
    g.fillRect(kTimelineLeftPadding, getHeight() - kFooterHeight, getWidth() - kTimelineLeftPadding, kFooterHeight);

    const auto maxTimelineSec = xToTime(getWidth());
    for (double beat = 0.0; beat <= maxTimelineSec + beatSec; beat += beatSec)
    {
        const auto x = kTimelineLeftPadding + static_cast<int>(beat * pixelsPerSecond_);
        const bool isBar = std::fmod(beat, barSec) < 0.0001 || std::abs(std::fmod(beat, barSec) - barSec) < 0.0001;
        g.setColour(isBar ? juce::Colour::fromRGB(58, 64, 72) : juce::Colour::fromRGB(36, 40, 46));
        g.drawLine(static_cast<float>(x), static_cast<float>(kHeaderHeight), static_cast<float>(x), static_cast<float>(getHeight() - kFooterHeight), isBar ? 1.4f : 0.7f);
        if (isBar)
        {
            const auto barIndex = static_cast<int>(std::floor(beat / barSec)) + 1;
            g.setColour(juce::Colours::white.withAlpha(0.96f));
            g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
            g.drawText(juce::String::formatted("%d.%d", barIndex, 1), x + 6, 6, 48, 18, juce::Justification::centredLeft);
        }
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
            ? juce::Colour::fromRGB(238, 176, 55)
            : (clip.activeTake ? juce::Colour::fromRGB(18, 172, 74) : juce::Colour::fromRGB(46, 127, 222));
        juce::ColourGradient clipGradient(
            baseColour.brighter(0.12f),
            static_cast<float>(x),
            static_cast<float>(clipY),
            baseColour.darker(0.18f),
            static_cast<float>(x),
            static_cast<float>(clipY + 48),
            false);
        g.setGradientFill(clipGradient);
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(clipY), static_cast<float>(width), 48.0f, 6.0f);
        for (double beat = 0.0; beat <= maxTimelineSec + beatSec; beat += beatSec)
        {
            const auto beatX = kTimelineLeftPadding + static_cast<int>(beat * pixelsPerSecond_);
            if (beatX <= x + 2 || beatX >= x + width - 2)
            {
                continue;
            }

            const bool isBar = std::fmod(beat, barSec) < 0.0001 || std::abs(std::fmod(beat, barSec) - barSec) < 0.0001;
            g.setColour(juce::Colours::black.withAlpha(isBar ? 0.22f : 0.11f));
            g.drawLine(static_cast<float>(beatX), static_cast<float>(clipY + 1), static_cast<float>(beatX), static_cast<float>(clipY + 47), isBar ? 1.0f : 0.7f);
        }
        g.setColour(baseColour.brighter(0.4f));
        g.drawRoundedRectangle(static_cast<float>(x), static_cast<float>(clipY), static_cast<float>(width), 48.0f, 6.0f, 1.2f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(clip.id, x + 8, clipY + 6, width - 16, 16, juce::Justification::centredLeft);
        if (clip.selected)
        {
            g.setColour(juce::Colours::white.withAlpha(0.82f));
            g.drawLine(static_cast<float>(x + 4), static_cast<float>(clipY + 4), static_cast<float>(x + 4), static_cast<float>(clipY + 13), 1.6f);
            g.drawLine(static_cast<float>(x + width - 4), static_cast<float>(clipY + 4), static_cast<float>(x + width - 4), static_cast<float>(clipY + 13), 1.6f);
            g.drawLine(static_cast<float>(x + 4), static_cast<float>(clipY + 44), static_cast<float>(x + 10), static_cast<float>(clipY + 38), 1.4f);
            g.drawLine(static_cast<float>(x + width - 4), static_cast<float>(clipY + 44), static_cast<float>(x + width - 10), static_cast<float>(clipY + 38), 1.4f);
        }
        if (!clip.takeGroupId.empty())
        {
            g.setColour(clip.activeTake ? juce::Colours::lightgreen : juce::Colours::lightgrey);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(clip.activeTake ? "ACTIVE TAKE" : "ALT TAKE", x + 8, clipY + 21, width - 16, 12, juce::Justification::centredLeft);
        }

        const auto assetPath = resolveAssetPath(state, clip);
        const auto waveform = waveformService_.requestWaveform(assetPath);
        g.setColour(juce::Colours::black.withAlpha(0.08f));
        g.fillRoundedRectangle(static_cast<float>(x + 4), static_cast<float>(clipY + 14), static_cast<float>(juce::jmax(20, width - 8)), 30.0f, 4.0f);
        const auto innerWidth = juce::jmax(1, width - 12);
        const auto bucketCount = juce::jmax(1, static_cast<int>(waveform.peaks.size()));
        const float centerY = static_cast<float>(clipY + 29.0f);
        juce::Path waveformFill;
        std::vector<juce::Point<float>> bottomPoints;
        const float amplitudeScale = pixelsPerSecond_ >= 180.0 ? 13.5f : (pixelsPerSecond_ >= 90.0 ? 11.5f : 9.5f);
        const float minAmplitude = pixelsPerSecond_ >= 180.0 ? 1.2f : (pixelsPerSecond_ >= 90.0 ? 1.9f : 2.8f);
        const auto sourceDuration = std::max(0.001, waveform.durationSec);
        bool startedWaveform = false;
        for (int i = 0; i < innerWidth; ++i)
        {
            const auto clipRatio = static_cast<double>(i) / static_cast<double>(juce::jmax(1, innerWidth - 1));
            const auto sourceTimeSec = std::clamp(clip.offsetSec + clip.durationSec * clipRatio, 0.0, sourceDuration);
            const auto sourceRatio = sourceTimeSec / sourceDuration;
            const auto bucketIndex = std::min(bucketCount - 1, static_cast<int>(sourceRatio * static_cast<double>(bucketCount - 1)));
            const auto safeIndex = static_cast<std::size_t>(bucketIndex);
            const float maxSample = waveform.maxs.empty() ? 0.0f : waveform.maxs[safeIndex];
            const float minSample = waveform.mins.empty() ? 0.0f : waveform.mins[safeIndex];
            const float topAmplitude = std::max(minAmplitude, std::abs(maxSample) * amplitudeScale);
            const float bottomAmplitude = std::max(minAmplitude, std::abs(minSample) * amplitudeScale);
            const float px = static_cast<float>(x + 6 + i);
            const float topY = centerY - topAmplitude;
            const float bottomY = centerY + bottomAmplitude;
            if (!startedWaveform)
            {
                waveformFill.startNewSubPath(px, topY);
                startedWaveform = true;
            }
            else
            {
                waveformFill.lineTo(px, topY);
            }
            bottomPoints.emplace_back(px, bottomY);
        }
        for (auto it = bottomPoints.rbegin(); it != bottomPoints.rend(); ++it)
        {
            waveformFill.lineTo(it->x, it->y);
        }
        waveformFill.closeSubPath();
        g.setColour(baseColour.darker(0.28f).withAlpha(0.88f));
        g.fillPath(waveformFill);
        g.setColour(baseColour.darker(0.48f).withAlpha(0.72f));
        g.strokePath(waveformFill, juce::PathStrokeType(0.55f));
        g.setColour(juce::Colours::black.withAlpha(0.12f));
        g.drawLine(static_cast<float>(x + 6), centerY, static_cast<float>(x + width - 6), centerY, 1.0f);

        if (clip.fadeInSec > 0.0)
        {
            const auto fadeWidth = juce::jmin(width / 2, static_cast<int>(clip.fadeInSec * pixelsPerSecond_));
            juce::Path fadeCurve;
            juce::Path fadeFill;
            fadeCurve.startNewSubPath(static_cast<float>(x + 2), static_cast<float>(clipY + 46));
            fadeFill.startNewSubPath(static_cast<float>(x + 2), static_cast<float>(clipY + 4));
            fadeFill.lineTo(static_cast<float>(x + 2), static_cast<float>(clipY + 46));
            for (int sample = 0; sample <= juce::jmax(4, fadeWidth); ++sample)
            {
                const auto ratio = static_cast<float>(sample) / static_cast<float>(juce::jmax(1, fadeWidth));
                const auto px = static_cast<float>(x + 2 + sample);
                const auto py = static_cast<float>(clipY + 46) - std::sin(ratio * juce::MathConstants<float>::halfPi) * 42.0f;
                fadeCurve.lineTo(px, py);
                fadeFill.lineTo(px, py);
            }
            fadeFill.lineTo(static_cast<float>(x + 2 + fadeWidth), static_cast<float>(clipY + 4));
            fadeFill.closeSubPath();
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.fillPath(fadeFill);
            g.setColour(juce::Colours::white.withAlpha(0.76f));
            g.strokePath(fadeCurve, juce::PathStrokeType(2.0f));
        }
        if (clip.fadeOutSec > 0.0)
        {
            const auto fadeWidth = juce::jmin(width / 2, static_cast<int>(clip.fadeOutSec * pixelsPerSecond_));
            juce::Path fadeCurve;
            juce::Path fadeFill;
            fadeCurve.startNewSubPath(static_cast<float>(x + width - fadeWidth), static_cast<float>(clipY + 4));
            fadeFill.startNewSubPath(static_cast<float>(x + width - fadeWidth), static_cast<float>(clipY + 4));
            for (int sample = 0; sample <= juce::jmax(4, fadeWidth); ++sample)
            {
                const auto ratio = static_cast<float>(sample) / static_cast<float>(juce::jmax(1, fadeWidth));
                const auto px = static_cast<float>(x + width - fadeWidth + sample);
                const auto py = static_cast<float>(clipY + 4) + (1.0f - std::cos(ratio * juce::MathConstants<float>::halfPi)) * 42.0f;
                fadeCurve.lineTo(px, py);
                fadeFill.lineTo(px, py);
            }
            fadeFill.lineTo(static_cast<float>(x + width), static_cast<float>(clipY + 46));
            fadeFill.lineTo(static_cast<float>(x + width), static_cast<float>(clipY + 4));
            fadeFill.closeSubPath();
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.fillPath(fadeFill);
            g.setColour(juce::Colours::white.withAlpha(0.76f));
            g.strokePath(fadeCurve, juce::PathStrokeType(2.0f));
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

            const auto overlapX = kTimelineLeftPadding + static_cast<int>(overlapStart * pixelsPerSecond_);
            const auto overlapW = juce::jmax(2, static_cast<int>((overlapEnd - overlapStart) * pixelsPerSecond_));
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
        const auto regionX = kTimelineLeftPadding + static_cast<int>(state.uiState.selectedRegionStartSec * pixelsPerSecond_);
        const auto regionW = juce::jmax(2, static_cast<int>((state.uiState.selectedRegionEndSec - state.uiState.selectedRegionStartSec) * pixelsPerSecond_));
        g.setColour(juce::Colours::yellow.withAlpha(0.18f));
        g.fillRect(regionX, 28, regionW, getHeight() - 36);
        g.setColour(juce::Colours::yellow);
        g.drawRect(regionX, 28, regionW, getHeight() - 36, 1);
    }

    const auto playheadTimelineSec = state.uiState.selectedClipId.empty()
        ? state.uiState.playheadSec
        : state.uiState.playheadSec;
    const auto playheadX = kTimelineLeftPadding + static_cast<int>(playheadTimelineSec * pixelsPerSecond_);
    g.setColour(transport_.isPlaying() ? juce::Colours::red : juce::Colours::orangered);
    g.drawLine(static_cast<float>(playheadX), static_cast<float>(kHeaderHeight), static_cast<float>(playheadX), static_cast<float>(getHeight() - kFooterHeight), 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.drawText(
        "Playhead " + juce::String(playheadTimelineSec, 2) + "s",
        kTimelineLeftPadding + 8,
        getHeight() - kFooterHeight + 4,
        180,
        kFooterHeight - 8,
        juce::Justification::centredLeft);
}

void TimelineView::mouseDown(const juce::MouseEvent& event)
{
    auto& state = projectManager_.state();
    draggingPlayhead_ = false;
    draggingRegion_ = false;
    draggingClip_ = false;
    clipMovedDuringDrag_ = false;
    clipDragMode_ = ClipDragMode::None;
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
            if (event.mods.isRightButtonDown())
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Split At Playhead");
                menu.addItem(2, "Delete Clip");
                menu.showMenuAsync(
                    juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(event.getScreenPosition(), {1, 1})),
                    [this](int result)
                    {
                        if (result == 1 && splitClipCallback_)
                        {
                            splitClipCallback_();
                        }
                        else if (result == 2 && deleteClipCallback_)
                        {
                            deleteClipCallback_();
                        }
                    });
                repaint();
                return;
            }

            clipDragMode_ = hitTestClipDragMode(clipArea, event.getPosition());
            if (beginClipDragCallback_)
            {
                beginClipDragCallback_();
            }
            draggingClip_ = true;
            draggedClipId_ = clip.id;
            clipDragOffsetSec_ = xToTime(event.x) - clip.startSec;
            lastDragTimelineSec_ = xToTime(event.x);
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
        auto* clip = draggedClip(state);
        if (clip == nullptr)
        {
            repaint();
            return;
        }

        const auto timelineSec = xToTime(event.x);
        bool changed = false;
        switch (clipDragMode_)
        {
        case ClipDragMode::Move:
            changed = timeline_.moveClip(state, draggedClipId_, timelineSec - clipDragOffsetSec_);
            break;
        case ClipDragMode::TrimLeft:
            if (trimLeftCallback_)
            {
                changed = trimLeftCallback_(timelineSec - lastDragTimelineSec_);
            }
            break;
        case ClipDragMode::TrimRight:
            if (trimRightCallback_)
            {
                changed = trimRightCallback_(timelineSec - lastDragTimelineSec_);
            }
            break;
        case ClipDragMode::FadeIn:
            if (setFadeInCallback_)
            {
                changed = setFadeInCallback_(std::max(0.0, timelineSec - clip->startSec));
            }
            break;
        case ClipDragMode::FadeOut:
            if (setFadeOutCallback_)
            {
                changed = setFadeOutCallback_(std::max(0.0, clipEndSec(*clip) - timelineSec));
            }
            break;
        case ClipDragMode::None:
            break;
        }
        lastDragTimelineSec_ = timelineSec;
        clipMovedDuringDrag_ = clipMovedDuringDrag_ || changed;
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
        clipDragMode_ = ClipDragMode::None;
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

void TimelineView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        if (wheel.deltaY > 0.0f)
        {
            zoomIn();
        }
        else if (wheel.deltaY < 0.0f)
        {
            zoomOut();
        }
        return;
    }

    juce::Component::mouseWheelMove(event, wheel);
}

void TimelineView::setPixelsPerSecond(double value)
{
    pixelsPerSecond_ = juce::jlimit(20.0, 400.0, value);
    updateContentSize();
    repaint();
}

void TimelineView::zoomIn()
{
    setPixelsPerSecond(pixelsPerSecond_ * 1.25);
}

void TimelineView::zoomOut()
{
    setPixelsPerSecond(pixelsPerSecond_ / 1.25);
}

void TimelineView::resetZoom()
{
    setPixelsPerSecond(100.0);
}

void TimelineView::updateContentSize()
{
    const auto& state = projectManager_.state();
    double durationSec = 60.0;
    for (const auto& clip : state.clips)
    {
        durationSec = std::max(durationSec, clip.startSec + clip.durationSec + 1.0);
    }

    const auto viewportWidth = getParentComponent() != nullptr ? getParentComponent()->getWidth() : 0;
    const auto viewportHeight = getParentComponent() != nullptr ? getParentComponent()->getHeight() : 0;
    const auto contentWidth = juce::jmax(juce::jmax(2400, viewportWidth), kTimelineLeftPadding + static_cast<int>(durationSec * pixelsPerSecond_));
    const auto naturalHeight = 70 + static_cast<int>(state.tracks.size()) * 80 + kFooterHeight;
    const auto contentHeight = juce::jmax(juce::jmax(360, viewportHeight), naturalHeight);
    if (getWidth() != contentWidth || getHeight() != contentHeight)
    {
        setSize(contentWidth, contentHeight);
    }
}

double TimelineView::xToTime(int x) const
{
    return juce::jmax(0.0, (static_cast<double>(x) - static_cast<double>(kTimelineLeftPadding)) / pixelsPerSecond_);
}

bool TimelineView::isRulerHit(const juce::Point<int>& point) const
{
    return point.y >= kHeaderHeight && point.y <= (kHeaderHeight + kRulerHeight) && point.x >= kTimelineLeftPadding;
}

int TimelineView::clipYForIndex(int index) const
{
    return 30 + index * 60;
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
        kTimelineLeftPadding + static_cast<int>(clip.startSec * pixelsPerSecond_),
        clipYForIndex(trackIndex),
        juce::jmax(120, static_cast<int>(clip.durationSec * pixelsPerSecond_)),
        48);
}

TimelineView::ClipDragMode TimelineView::hitTestClipDragMode(const juce::Rectangle<int>& clipArea, const juce::Point<int>& point) const
{
    const auto localX = point.x - clipArea.getX();
    const auto localY = point.y - clipArea.getY();
    const bool nearLeft = localX <= 14;
    const bool nearRight = localX >= clipArea.getWidth() - 14;
    const bool topZone = localY <= 18;
    const bool bottomZone = localY >= clipArea.getHeight() - 18;

    if (nearLeft && topZone)
    {
        return ClipDragMode::TrimLeft;
    }
    if (nearRight && topZone)
    {
        return ClipDragMode::TrimRight;
    }
    if (nearLeft && bottomZone)
    {
        return ClipDragMode::FadeIn;
    }
    if (nearRight && bottomZone)
    {
        return ClipDragMode::FadeOut;
    }
    return ClipDragMode::Move;
}

moon::engine::ClipInfo* TimelineView::draggedClip(moon::engine::ProjectState& state)
{
    for (auto& clip : state.clips)
    {
        if (clip.id == draggedClipId_)
        {
            return &clip;
        }
    }
    return nullptr;
}
}
#endif
