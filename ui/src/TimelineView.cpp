#include "TimelineView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#if MOON_HAS_JUCE
namespace moon::ui
{
namespace
{
constexpr int kTimelineLeftPadding = 0;
constexpr int kHeaderHeight = 0;
constexpr int kRulerHeight = 30;
constexpr int kFooterHeight = 0;
constexpr int kWaveformInsetX = 2;
constexpr int kWaveformInsetTop = 4;
constexpr int kWaveformInsetBottom = 4;
constexpr int kClipMoveBandHeight = 10;

int selectionLaneTopForIndex(int trackIndex)
{
    const auto clipTop = moon::ui::layout::trackRowY(trackIndex);
    if (trackIndex <= 0)
    {
        return clipTop;
    }

    return clipTop - (moon::ui::layout::kTrackRowStep - moon::ui::layout::kClipRowHeight);
}

juce::Colour parseTrackColour(const std::string& colorHex)
{
    if (colorHex.size() == 7 && colorHex.front() == '#')
    {
        return juce::Colour::fromString(juce::String("ff") + juce::String(colorHex.substr(1)));
    }
    return juce::Colour::fromRGB(45, 150, 255);
}

juce::Colour defaultTrackColour(int index)
{
    static constexpr std::array<juce::uint32, 8> palette{
        0xff2d96ffu,
        0xffff2b8au,
        0xff18c458u,
        0xffebc414u,
        0xff8c66ffu,
        0xffff8740u,
        0xff19c2cau,
        0xffff5e5eu};
    return juce::Colour(palette[static_cast<std::size_t>(index) % palette.size()]);
}

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

juce::String resolveClipDisplayName(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip)
{
    const auto assetPath = resolveAssetPath(state, clip);
    if (!assetPath.empty())
    {
        const auto stem = std::filesystem::path(assetPath).stem().string();
        if (!stem.empty())
        {
            return juce::String(stem);
        }
    }

    return juce::String(clip.id);
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

juce::Colour waveformPlaceholderFor(const juce::Colour& baseColour)
{
    return baseColour.contrasting(0.82f).withAlpha(0.72f);
}

juce::Colour waveformStrokeFor(const juce::Colour& baseColour)
{
    return baseColour.contrasting(0.78f).withAlpha(0.82f);
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

double gridDivisionSec(TimelineGridMode mode, double tempo, int numerator, int denominator)
{
    const auto beatSec = beatDurationSec(tempo, denominator);
    const auto barSec = barDurationSec(tempo, numerator, denominator);
    switch (mode)
    {
    case TimelineGridMode::None:      return 0.0;
    case TimelineGridMode::StepDiv6:  return beatSec / 24.0;
    case TimelineGridMode::StepDiv4:  return beatSec / 16.0;
    case TimelineGridMode::StepDiv3:  return beatSec / 12.0;
    case TimelineGridMode::StepDiv2:  return beatSec / 8.0;
    case TimelineGridMode::Step:      return beatSec / 4.0;
    case TimelineGridMode::BeatDiv6:  return beatSec / 6.0;
    case TimelineGridMode::BeatDiv4:  return beatSec / 4.0;
    case TimelineGridMode::BeatDiv3:  return beatSec / 3.0;
    case TimelineGridMode::BeatDiv2:  return beatSec / 2.0;
    case TimelineGridMode::Beat:      return beatSec;
    case TimelineGridMode::Bar:       return barSec;
    }

    return beatSec;
}

struct TimelineGridLod
{
    bool showBeatLines { true };
    bool showSubdivisionLines { false };
    float majorThickness { 1.15f };
    float beatThickness { 0.65f };
    float subdivisionThickness { 0.45f };
    float oddBarAlpha { 0.32f };
    float evenBarAlpha { 0.20f };
};

TimelineGridLod computeTimelineGridLod(float barWidthPx, float beatWidthPx, float gridWidthPx)
{
    TimelineGridLod lod{};

    if (barWidthPx < 22.0f)
    {
        lod.showBeatLines = false;
        lod.showSubdivisionLines = false;
        lod.majorThickness = 0.85f;
        lod.beatThickness = 0.0f;
        lod.subdivisionThickness = 0.0f;
        lod.oddBarAlpha = 0.18f;
        lod.evenBarAlpha = 0.14f;
        return lod;
    }

    if (barWidthPx < 36.0f)
    {
        lod.showBeatLines = false;
        lod.showSubdivisionLines = false;
        lod.majorThickness = 0.88f;
        lod.beatThickness = 0.0f;
        lod.subdivisionThickness = 0.0f;
        lod.oddBarAlpha = 0.18f;
        lod.evenBarAlpha = 0.14f;
        return lod;
    }

    if (barWidthPx < 60.0f)
    {
        lod.showBeatLines = true;
        lod.showSubdivisionLines = false;
        lod.majorThickness = 0.90f;
        lod.beatThickness = 0.48f;
        lod.subdivisionThickness = 0.0f;
        lod.oddBarAlpha = 0.18f;
        lod.evenBarAlpha = 0.14f;
        return lod;
    }

    if (barWidthPx < 110.0f)
    {
        lod.showBeatLines = true;
        lod.showSubdivisionLines = false;
        lod.majorThickness = 0.92f;
        lod.beatThickness = 0.50f;
        lod.subdivisionThickness = 0.0f;
        lod.oddBarAlpha = 0.18f;
        lod.evenBarAlpha = 0.14f;
        return lod;
    }

    lod.showBeatLines = true;
    lod.showSubdivisionLines = gridWidthPx >= 12.0f;
    lod.majorThickness = 0.95f;
    lod.beatThickness = 0.52f;
    lod.subdivisionThickness = 0.26f;
    lod.oddBarAlpha = 0.18f;
    lod.evenBarAlpha = 0.14f;
    return lod;
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
    , waveformTileCache_(waveformService)
{
    waveformService_.addListener(this);
}

TimelineView::~TimelineView()
{
    waveformRendererOpenGL_.detach();
    waveformService_.removeListener(this);
}

void TimelineView::resized()
{
    updateContentSize();
}

void TimelineView::paint(juce::Graphics& g)
{
    paintTimelineBase(g);
    paintWaveformLayer(g);
}

void TimelineView::paintTimelineBase(juce::Graphics& g)
{
    const auto& state = projectManager_.state();
    const auto tempo = juce::jmax(30.0, state.tempo);
    const auto numerator = juce::jmax(1, state.timeSignatureNumerator);
    const auto denominator = juce::jmax(1, state.timeSignatureDenominator);

    const double beatSec = beatDurationSec(tempo, denominator);
    const double barSec = barDurationSec(tempo, numerator, denominator);
    const double gridSec = gridDivisionSec(gridMode_, tempo, numerator, denominator);

    const float beatWidthPx = static_cast<float>(beatSec * pixelsPerSecond_);
    const float barWidthPx = static_cast<float>(barSec * pixelsPerSecond_);
    const float gridWidthPx = gridSec > 0.0 ? static_cast<float>(gridSec * pixelsPerSecond_) : 0.0f;
    const auto lod = computeTimelineGridLod(barWidthPx, beatWidthPx, gridWidthPx);

    const auto visibleArea = g.getClipBounds();
    const auto visibleStartSec = juce::jmax(0.0, xToTime(visibleArea.getX() - 8));
    const auto visibleEndSec = juce::jmax(visibleStartSec, xToTime(visibleArea.getRight() + 8));
    const auto firstBarIndex = juce::jmax(0, static_cast<int>(std::floor(visibleStartSec / barSec)) - 1);
    const auto lastBarIndex = juce::jmax(firstBarIndex, static_cast<int>(std::ceil(visibleEndSec / barSec)) + 1);
    const auto firstBeatIndex = juce::jmax(0, static_cast<int>(std::floor(visibleStartSec / beatSec)) - 1);
    const auto lastBeatIndex = juce::jmax(firstBeatIndex, static_cast<int>(std::ceil(visibleEndSec / beatSec)) + 1);

    g.fillAll(juce::Colour::fromRGB(18, 23, 29));

    for (int barIndex = firstBarIndex; barIndex <= lastBarIndex; ++barIndex)
    {
        const auto barStartSec = static_cast<double>(barIndex) * barSec;
        const auto barEndSec = barStartSec + barSec;
        const auto barX = kTimelineLeftPadding + static_cast<int>(std::round(barStartSec * pixelsPerSecond_));
        const auto barW = juce::jmax(1, static_cast<int>(std::round((barEndSec - barStartSec) * pixelsPerSecond_)));

        juce::Colour bandColour = ((barIndex % 2) == 0)
            ? juce::Colour::fromRGB(26, 33, 41).withAlpha(lod.oddBarAlpha) : juce::Colour::fromRGB(20, 26, 33).withAlpha(lod.evenBarAlpha);

        g.setColour(bandColour);
        g.fillRect(barX, kHeaderHeight, barW, getHeight() - kFooterHeight);
    }

    if (lod.showSubdivisionLines && gridSec > 0.0 && gridSec < beatSec)
    {
        const auto firstGridIndex = juce::jmax(0, static_cast<int>(std::floor(visibleStartSec / gridSec)) - 1);
        const auto lastGridIndex = juce::jmax(firstGridIndex, static_cast<int>(std::ceil(visibleEndSec / gridSec)) + 1);

        for (int gridIndex = firstGridIndex; gridIndex <= lastGridIndex; ++gridIndex)
        {
            const auto gridPosSec = static_cast<double>(gridIndex) * gridSec;
            const auto x = kTimelineLeftPadding + static_cast<int>(std::round(gridPosSec * pixelsPerSecond_));

            const bool isBar = std::abs(std::remainder(gridPosSec, barSec)) < 0.0001;
            const bool isBeat = std::abs(std::remainder(gridPosSec, beatSec)) < 0.0001;
            if (isBar || isBeat)
            {
                continue;
            }

            g.setColour(juce::Colour::fromRGB(41, 48, 56).withAlpha(0.50f));
            g.drawLine(
                static_cast<float>(x),
                static_cast<float>(kHeaderHeight),
                static_cast<float>(x),
                static_cast<float>(getHeight() - kFooterHeight),
                lod.subdivisionThickness);
        }
    }

    for (int beatIndex = firstBeatIndex; beatIndex <= lastBeatIndex; ++beatIndex)
    {
        const auto beat = static_cast<double>(beatIndex) * beatSec;
        const auto x = kTimelineLeftPadding + static_cast<int>(std::round(beat * pixelsPerSecond_));
        const bool isBar = (beatIndex % numerator) == 0;

        if (isBar)
        {
            g.setColour(juce::Colour::fromRGB(58, 68, 79).withAlpha(0.50f));
            g.drawLine(
                static_cast<float>(x),
                static_cast<float>(kHeaderHeight),
                static_cast<float>(x),
                static_cast<float>(getHeight() - kFooterHeight),
                lod.majorThickness);
        }
        else if (lod.showBeatLines)
        {
            g.setColour(juce::Colour::fromRGB(54, 64, 75).withAlpha(0.50f));
            g.drawLine(
                static_cast<float>(x),
                static_cast<float>(kHeaderHeight),
                static_cast<float>(x),
                static_cast<float>(getHeight() - kFooterHeight),
                lod.beatThickness);
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
        if (!bounds.intersects(visibleArea.expanded(24, 0)))
        {
            continue;
        }

        const int x = bounds.getX();
        const int width = bounds.getWidth();
        const int clipY = clipYForIndex(trackIndex);
        const auto baseColour = trackAccentForClip(state, clip, clip.selected);

        juce::ColourGradient clipGradient(
            baseColour.brighter(0.12f),
            static_cast<float>(x),
            static_cast<float>(clipY),
            baseColour.darker(0.18f),
            static_cast<float>(x),
            static_cast<float>(clipY + moon::ui::layout::kClipRowHeight),
            false);
        g.setGradientFill(clipGradient);
        g.fillRoundedRectangle(
            static_cast<float>(x),
            static_cast<float>(clipY),
            static_cast<float>(width),
            static_cast<float>(moon::ui::layout::kClipRowHeight),
            6.0f);

        const auto clipStartSec = juce::jmax(0.0, static_cast<double>(x - kTimelineLeftPadding) / pixelsPerSecond_);
        const auto clipEndVisibleSec = clipStartSec + static_cast<double>(width) / pixelsPerSecond_;
        const auto clipFirstBeatIndex = juce::jmax(0, static_cast<int>(std::floor(clipStartSec / beatSec)) - 1);
        const auto clipLastBeatIndex = juce::jmax(clipFirstBeatIndex, static_cast<int>(std::ceil(clipEndVisibleSec / beatSec)) + 1);

        for (int beatIndex = clipFirstBeatIndex; beatIndex <= clipLastBeatIndex; ++beatIndex)
        {
            const auto beat = static_cast<double>(beatIndex) * beatSec;
            const auto beatX = kTimelineLeftPadding + static_cast<int>(std::round(beat * pixelsPerSecond_));
            if (beatX <= x + 2 || beatX >= x + width - 2)
            {
                continue;
            }

            const bool isBar = (beatIndex % numerator) == 0;
            g.setColour(juce::Colours::black.withAlpha(isBar ? 0.12f : 0.05f));
            g.drawLine(
                static_cast<float>(beatX),
                static_cast<float>(clipY + 1),
                static_cast<float>(beatX),
                static_cast<float>(clipY + moon::ui::layout::kClipRowHeight - 1),
                isBar ? 1.0f : 0.7f);
        }

        g.setColour(baseColour.brighter(0.4f));
        g.drawRoundedRectangle(
            static_cast<float>(x),
            static_cast<float>(clipY),
            static_cast<float>(width),
            static_cast<float>(moon::ui::layout::kClipRowHeight),
            6.0f,
            1.2f);
    }
}

void TimelineView::paintOverChildren(juce::Graphics& g)
{
    paintClipOverlays(g);
}

void TimelineView::paintWaveformLayer(juce::Graphics& g)
{
    const auto& state = projectManager_.state();
    const auto visibleArea = g.getClipBounds();
    std::vector<WaveformRendererOpenGL::DrawItem> gpuWaveformItems;

    for (const auto& clip : state.clips)
    {
        const auto bounds = clipBounds(state, clip);
        if (!bounds.intersects(visibleArea.expanded(24, 0)))
        {
            continue;
        }

        const auto baseColour = trackAccentForClip(state, clip, clip.selected);
        const auto waveformColour = waveformStrokeFor(baseColour);
        auto waveformBatch = waveformTileCache_.prepareVisibleTiles(
            clip,
            resolveAssetPath(state, clip),
            bounds,
            visibleArea,
            pixelsPerSecond_,
            waveformDetailScale_,
            {
                waveformColour,
                waveformPlaceholderFor(baseColour)
            });

        if (waveformRendererOpenGL_.isReady())
        {
            gpuWaveformItems.insert(
                gpuWaveformItems.end(),
                waveformBatch.gpuItems.begin(),
                waveformBatch.gpuItems.end());
        }
        else
        {
            waveformTileCache_.drawCpuFallback(g, waveformBatch);
        }
    }

    lastWaveformBatch_.gpuItems = gpuWaveformItems;
    lastWaveformBatch_.needsCpuFallback = !waveformRendererOpenGL_.isReady();
    if (waveformRendererOpenGL_.isReady())
    {
        waveformRendererOpenGL_.setVisibleTiles(std::move(gpuWaveformItems));
    }
    else
    {
        waveformRendererOpenGL_.setVisibleTiles({});
    }
}

void TimelineView::paintClipOverlays(juce::Graphics& g)
{
    const auto& state = projectManager_.state();
    const auto visibleArea = g.getClipBounds();

    for (const auto& clip : state.clips)
    {
        const auto bounds = clipBounds(state, clip);
        if (!bounds.intersects(visibleArea.expanded(24, 0)))
        {
            continue;
        }

        const int x = bounds.getX();
        const int width = bounds.getWidth();
        const int clipY = bounds.getY();
        const int labelInset = clip.selected ? 18 : 8;
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(resolveClipDisplayName(state, clip), x + labelInset, clipY + 4, width - (labelInset + 8), 16, juce::Justification::centredLeft);
        if (clip.selected)
        {
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawLine(static_cast<float>(x + 5), static_cast<float>(clipY + 4), static_cast<float>(x + 5), static_cast<float>(clipY + 15), 2.0f);
            g.drawLine(static_cast<float>(x + width - 5), static_cast<float>(clipY + 4), static_cast<float>(x + width - 5), static_cast<float>(clipY + 15), 2.0f);
            g.drawLine(static_cast<float>(x + 5), static_cast<float>(clipY + moon::ui::layout::kClipRowHeight - 5), static_cast<float>(x + 13), static_cast<float>(clipY + moon::ui::layout::kClipRowHeight - 13), 1.8f);
            g.drawLine(static_cast<float>(x + width - 5), static_cast<float>(clipY + moon::ui::layout::kClipRowHeight - 5), static_cast<float>(x + width - 13), static_cast<float>(clipY + moon::ui::layout::kClipRowHeight - 13), 1.8f);
            g.setColour(juce::Colours::black.withAlpha(0.18f));
            g.drawLine(static_cast<float>(x + 7), static_cast<float>(clipY + 4), static_cast<float>(x + 7), static_cast<float>(clipY + 15), 1.0f);
            g.drawLine(static_cast<float>(x + width - 7), static_cast<float>(clipY + 4), static_cast<float>(x + width - 7), static_cast<float>(clipY + 15), 1.0f);
        }
        if (!clip.takeGroupId.empty())
        {
            g.setColour(clip.activeTake ? juce::Colours::lightgreen : juce::Colours::lightgrey);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(clip.activeTake ? "ACTIVE TAKE" : "ALT TAKE", x + 8, clipY + 18, width - 16, 12, juce::Justification::centredLeft);
        }

        if (clip.fadeInSec > 0.0)
        {
            const auto fadeWidth = juce::jmin(width / 2, static_cast<int>(clip.fadeInSec * pixelsPerSecond_));
            juce::Path fadeCurve;
            juce::Path fadeFill;
            const auto clipTop = static_cast<float>(clipY);
            const auto clipBottom = static_cast<float>(clipY + moon::ui::layout::kClipRowHeight);
            const auto clipHeight = clipBottom - clipTop;
            fadeCurve.startNewSubPath(static_cast<float>(x), clipBottom);
            fadeFill.startNewSubPath(static_cast<float>(x), clipTop);
            fadeFill.lineTo(static_cast<float>(x), clipBottom);
            for (int sample = 0; sample <= juce::jmax(4, fadeWidth); ++sample)
            {
                const auto ratio = static_cast<float>(sample) / static_cast<float>(juce::jmax(1, fadeWidth));
                const auto px = static_cast<float>(x + sample);
                const auto easedRatio = 0.5f - 0.5f * std::cos(ratio * juce::MathConstants<float>::pi);
                const auto py = clipBottom - easedRatio * clipHeight;
                fadeCurve.lineTo(px, py);
                fadeFill.lineTo(px, py);
            }
            fadeFill.lineTo(static_cast<float>(x + fadeWidth), clipTop);
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
            const auto clipTop = static_cast<float>(clipY);
            const auto clipBottom = static_cast<float>(clipY + moon::ui::layout::kClipRowHeight);
            const auto clipHeight = clipBottom - clipTop;
            fadeCurve.startNewSubPath(static_cast<float>(x + width - fadeWidth), clipTop);
            fadeFill.startNewSubPath(static_cast<float>(x + width - fadeWidth), clipTop);
            for (int sample = 0; sample <= juce::jmax(4, fadeWidth); ++sample)
            {
                const auto ratio = static_cast<float>(sample) / static_cast<float>(juce::jmax(1, fadeWidth));
                const auto px = static_cast<float>(x + width - fadeWidth + sample);
                const auto easedRatio = 0.5f - 0.5f * std::cos(ratio * juce::MathConstants<float>::pi);
                const auto py = clipTop + easedRatio * clipHeight;
                fadeCurve.lineTo(px, py);
                fadeFill.lineTo(px, py);
            }
            fadeFill.lineTo(static_cast<float>(x + width), clipBottom);
            fadeFill.lineTo(static_cast<float>(x + width), clipTop);
            fadeFill.closeSubPath();
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.fillPath(fadeFill);
            g.setColour(juce::Colours::white.withAlpha(0.76f));
            g.strokePath(fadeCurve, juce::PathStrokeType(2.0f));
        }
    }

    if (!dropHoverTrackId_.empty())
    {
        const auto trackIndex = [&state, this]() -> int
        {
            for (std::size_t i = 0; i < state.tracks.size(); ++i)
            {
                if (state.tracks[i].id == dropHoverTrackId_)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }();

        if (trackIndex >= 0)
        {
            const auto hoverY = clipYForIndex(trackIndex);
            g.setColour(juce::Colour::fromRGB(83, 203, 255).withAlpha(0.12f));
            g.fillRect(visibleArea.getX(), hoverY, visibleArea.getWidth(), moon::ui::layout::kClipRowHeight);
            g.setColour(juce::Colour::fromRGB(83, 203, 255).withAlpha(0.82f));
            g.drawLine(static_cast<float>(visibleArea.getX()), static_cast<float>(hoverY + moon::ui::layout::kClipRowHeight), static_cast<float>(visibleArea.getRight()), static_cast<float>(hoverY + moon::ui::layout::kClipRowHeight), 1.6f);
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
                ? juce::Colours::gold.withAlpha(0.20f)
                : juce::Colours::white.withAlpha(0.10f);
            g.setColour(highlightColour);
            g.fillRoundedRectangle(static_cast<float>(overlapX), static_cast<float>(clipY + 8), static_cast<float>(overlapW), 12.0f, 4.0f);
            g.setColour(highlightColour.brighter(0.3f));
            g.drawRoundedRectangle(static_cast<float>(overlapX), static_cast<float>(clipY + 8), static_cast<float>(overlapW), 12.0f, 4.0f, 1.0f);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText("XF", overlapX + 4, clipY + 7, juce::jmax(18, overlapW - 8), 14, juce::Justification::centredLeft);
        }
    }

    if (state.uiState.hasSelectedRegion)
    {
        const auto regionX = kTimelineLeftPadding + static_cast<int>(state.uiState.selectedRegionStartSec * pixelsPerSecond_);
        const auto regionW = juce::jmax(2, static_cast<int>((state.uiState.selectedRegionEndSec - state.uiState.selectedRegionStartSec) * pixelsPerSecond_));
        int regionStartTrackIndex = state.uiState.selectedRegionStartTrackIndex;
        int regionEndTrackIndex = state.uiState.selectedRegionEndTrackIndex;

        if ((!state.tracks.empty()) && (regionStartTrackIndex < 0 || regionEndTrackIndex < 0))
        {
            if (!state.uiState.selectedTrackId.empty())
            {
                for (std::size_t i = 0; i < state.tracks.size(); ++i)
                {
                    if (state.tracks[i].id == state.uiState.selectedTrackId)
                    {
                        regionStartTrackIndex = static_cast<int>(i);
                        regionEndTrackIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (regionStartTrackIndex < 0 || regionEndTrackIndex < 0)
            {
                // Older sessions may only have time-range data; keep the region visible instead of hiding it.
                regionStartTrackIndex = 0;
                regionEndTrackIndex = static_cast<int>(state.tracks.size()) - 1;
            }
        }

        if (regionStartTrackIndex >= 0 && regionEndTrackIndex >= 0 && !state.tracks.empty())
        {
            regionStartTrackIndex = juce::jlimit(0, static_cast<int>(state.tracks.size()) - 1, regionStartTrackIndex);
            regionEndTrackIndex = juce::jlimit(0, static_cast<int>(state.tracks.size()) - 1, regionEndTrackIndex);
            const auto topTrackIndex = std::min(regionStartTrackIndex, regionEndTrackIndex);
            const auto bottomTrackIndex = std::max(regionStartTrackIndex, regionEndTrackIndex);
            const auto regionY = selectionLaneTopForIndex(topTrackIndex);
            const auto regionBottom = clipYForIndex(bottomTrackIndex) + moon::ui::layout::kClipRowHeight;

            g.setColour(juce::Colours::yellow.withAlpha(0.18f));
            g.fillRect(regionX, regionY, regionW, regionBottom - regionY);
            g.setColour(juce::Colours::yellow);
            g.drawRect(regionX, regionY, regionW, regionBottom - regionY, 1);
        }
    }

    const auto playheadX = kTimelineLeftPadding + static_cast<int>(state.uiState.playheadSec * pixelsPerSecond_);
    g.setColour(juce::Colour::fromRGB(255, 182, 32).withAlpha(0.95f));
    g.drawLine(static_cast<float>(playheadX), static_cast<float>(kHeaderHeight), static_cast<float>(playheadX), static_cast<float>(getHeight() - kFooterHeight), 1.6f);
}

void TimelineView::mouseDown(const juce::MouseEvent& event)
{
    auto& state = projectManager_.state();
    draggingPlayhead_ = false;
    draggingRegion_ = false;
    pendingRegionDrag_ = false;
    draggingClip_ = false;
    clipMovedDuringDrag_ = false;
    clipDragMode_ = ClipDragMode::None;
    draggedClipId_.clear();
    dragStartTrackIndex_ = -1;
    dragAnchorPoint_ = event.getPosition();

    for (std::size_t i = 0; i < state.clips.size(); ++i)
    {
        const auto& clip = state.clips[i];
        const auto clipArea = clipBounds(state, clip);

        if (clipArea.contains(event.getPosition()))
        {
            timeline_.selectTrack(state, clip.trackId);
            timeline_.selectClip(state, clip.id);
            timeline_.clearSelectedRegion(state);
            if (event.mods.isRightButtonDown())
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Split At Playhead");
                menu.addItem(2, "Delete Clip");
                menu.showMenuAsync(
                    juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
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

            const auto localY = event.y - clipArea.getY();
            if (localY > kClipMoveBandHeight)
            {
                pendingRegionDrag_ = true;
                draggingRegion_ = false;
                dragStartSec_ = xToTime(event.x);
                dragStartTrackIndex_ = trackIndexForY(event.y);
                dragAnchorPoint_ = event.getPosition();
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

    const auto clickedTrackId = nearestTrackIdForY(event.y);
    dragStartTrackIndex_ = trackIndexForY(event.y);
    if (!clickedTrackId.empty())
    {
        timeline_.selectTrack(state, clickedTrackId);
    }
    timeline_.clearSelectedRegion(state);
    pendingRegionDrag_ = true;
    dragStartSec_ = xToTime(event.x);
    repaint();
}

void TimelineView::mouseDrag(const juce::MouseEvent& event)
{
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
        {
            const auto targetTrackId = nearestTrackIdForY(event.y);
            changed = timeline_.moveClipToTrack(state, draggedClipId_, targetTrackId, timelineSec - clipDragOffsetSec_);
            dropHoverTrackId_ = targetTrackId;
            break;
        }
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

    if (pendingRegionDrag_)
    {
        const auto dragDelta = event.getPosition() - dragAnchorPoint_;
        // Empty-lane click should select the track; region selection only starts after a real drag.
        if (std::abs(dragDelta.x) < 3 && std::abs(dragDelta.y) < 3)
        {
            return;
        }

        pendingRegionDrag_ = false;
        draggingRegion_ = true;
    }

    if (!draggingRegion_)
    {
        return;
    }

    timeline_.setSelectedRegion(
        projectManager_.state(),
        dragStartSec_,
        xToTime(event.x),
        dragStartTrackIndex_,
        trackIndexForY(event.y));
    repaint();
}

void TimelineView::mouseUp(const juce::MouseEvent& event)
{
    if (draggingClip_)
    {
        draggingClip_ = false;
        dropHoverTrackId_.clear();
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

    if (pendingRegionDrag_)
    {
        pendingRegionDrag_ = false;
        repaint();
        return;
    }

    if (!draggingRegion_)
    {
        return;
    }

    draggingRegion_ = false;
    timeline_.setSelectedRegion(
        projectManager_.state(),
        dragStartSec_,
        xToTime(event.x),
        dragStartTrackIndex_,
        trackIndexForY(event.y));
    repaint();
}

void TimelineView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        const auto anchorSec = xToTime(event.x);
        int anchorViewportX = event.x;
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            anchorViewportX = event.x - viewport->getViewPositionX();
        }
        if (wheel.deltaY > 0.0f)
        {
            setPixelsPerSecond(pixelsPerSecond_ * 1.25, anchorSec, anchorViewportX);
        }
        else if (wheel.deltaY < 0.0f)
        {
            setPixelsPerSecond(pixelsPerSecond_ / 1.25, anchorSec, anchorViewportX);
        }
        return;
    }

    juce::Component::mouseWheelMove(event, wheel);
}

void TimelineView::setPixelsPerSecond(double value)
{
    setPixelsPerSecond(value, std::nullopt, std::nullopt);
}

void TimelineView::setPixelsPerSecond(double value, std::optional<double> anchorTimelineSec, std::optional<int> anchorViewX)
{
    const auto clampedPixelsPerSecond = juce::jlimit(1.0, 500.0, value);
    pixelsPerSecond_ = clampedPixelsPerSecond;
    updateContentSize();

    if (anchorTimelineSec.has_value() && anchorViewX.has_value())
    {
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            const auto newScrollX = juce::jmax(
                0,
                static_cast<int>(std::round(kTimelineLeftPadding + anchorTimelineSec.value() * clampedPixelsPerSecond - static_cast<double>(anchorViewX.value()))));
            viewport->setViewPosition(newScrollX, viewport->getViewPositionY());
        }
    }

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

void TimelineView::setGridMode(TimelineGridMode mode)
{
    if (gridMode_ == mode)
    {
        return;
    }

    gridMode_ = mode;
    repaint();
}

void TimelineView::repaintPlayheadDelta(double previousPlayheadSec, double nextPlayheadSec)
{
    const auto previousX = kTimelineLeftPadding + static_cast<int>(std::round(previousPlayheadSec * pixelsPerSecond_));
    const auto nextX = kTimelineLeftPadding + static_cast<int>(std::round(nextPlayheadSec * pixelsPerSecond_));
    const auto dirtyLeft = std::min(previousX, nextX) - 4;
    const auto dirtyWidth = std::abs(nextX - previousX) + 8;
    repaint(dirtyLeft, 0, juce::jmax(12, dirtyWidth), getHeight());
}

void TimelineView::setWaveformDetailScale(double value)
{
    const auto clampedValue = juce::jlimit(0.15, 10.0, value);
    if (std::abs(waveformDetailScale_ - clampedValue) < 0.0001)
    {
        return;
    }

    waveformDetailScale_ = clampedValue;
    waveformTileCache_.invalidateAll();
    waveformRendererOpenGL_.invalidateAll();
    repaint();
}

void TimelineView::updateContentSize()
{
    const auto& state = projectManager_.state();
    double durationSec = 60.0;
    for (const auto& clip : state.clips)
    {
        durationSec = std::max(durationSec, clip.startSec + clip.durationSec + 1.0);
    }

    auto* viewport = findParentComponentOfClass<juce::Viewport>();
    auto* sizingParent = viewport != nullptr ? static_cast<juce::Component*>(viewport) : getParentComponent();
    const auto viewportWidth = sizingParent != nullptr ? sizingParent->getWidth() : 0;
    const auto viewportHeight = sizingParent != nullptr ? sizingParent->getHeight() : 0;

    const auto contentWidth = juce::jmax(
        juce::jmax(2400, viewportWidth),
        kTimelineLeftPadding + static_cast<int>(durationSec * pixelsPerSecond_));

    const auto naturalHeight = moon::ui::layout::trackContentHeight(static_cast<int>(state.tracks.size())) + kFooterHeight;
    const auto contentHeight = juce::jmax(
        juce::jmax(360, viewportHeight),
        naturalHeight);

    if (getWidth() != contentWidth || getHeight() != contentHeight)
    {
        setSize(contentWidth, contentHeight);
    }
}

double TimelineView::xToTime(int x) const
{
    return juce::jmax(0.0, (static_cast<double>(x) - static_cast<double>(kTimelineLeftPadding)) / pixelsPerSecond_);
}

std::string TimelineView::nearestTrackIdForY(int contentY) const
{
    const auto& tracks = projectManager_.state().tracks;
    if (tracks.empty())
    {
        return {};
    }

    const auto clampedIndex = trackIndexForY(contentY);
    return tracks[static_cast<std::size_t>(clampedIndex)].id;
}

int TimelineView::trackIndexForY(int contentY) const
{
    const auto& tracks = projectManager_.state().tracks;
    if (tracks.empty())
    {
        return -1;
    }

    return juce::jlimit(
        0,
        static_cast<int>(tracks.size()) - 1,
        static_cast<int>(std::floor(static_cast<double>(contentY - moon::ui::layout::kTrackRowStartY) / static_cast<double>(moon::ui::layout::kTrackRowStep))));
}

double TimelineView::timeForContentX(int contentX) const
{
    return xToTime(contentX);
}

void TimelineView::setDropHoverTrackId(const std::string& trackId)
{
    if (dropHoverTrackId_ == trackId)
    {
        return;
    }

    dropHoverTrackId_ = trackId;
    repaint(getLocalBounds());
}

bool TimelineView::isRulerHit(const juce::Point<int>& point) const
{
    juce::ignoreUnused(point);
    return false;
}

int TimelineView::clipYForIndex(int index) const
{
    return moon::ui::layout::trackRowY(index);
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
        juce::jmax(18, static_cast<int>(std::round(clip.durationSec * pixelsPerSecond_))),
        moon::ui::layout::kClipRowHeight);
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

juce::Colour TimelineView::trackAccentForClip(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip, bool selected) const
{
    int trackIndex = 0;
    for (std::size_t i = 0; i < state.tracks.size(); ++i)
    {
        if (state.tracks[i].id != clip.trackId)
        {
            continue;
        }

        auto colour = state.tracks[i].colorHex.empty() ? defaultTrackColour(static_cast<int>(i)) : parseTrackColour(state.tracks[i].colorHex);
        if (selected)
        {
            colour = colour.brighter(0.18f);
        }
        return colour;
    }

    return selected ? juce::Colour::fromRGB(238, 176, 55) : juce::Colour::fromRGB(45, 150, 255);
}

void TimelineView::waveformSourceUpdated(const std::string& path)
{
    waveformRendererOpenGL_.invalidateRevisions(waveformTileCache_.invalidateSource(path));
    repaintWaveformSource(path);
}

void TimelineView::repaintWaveformSource(const std::string& path)
{
    const auto& state = projectManager_.state();
    juce::Rectangle<int> visibleBounds = getLocalBounds();
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        visibleBounds = viewport->getViewArea();
    }

    for (const auto& clip : state.clips)
    {
        if (resolveAssetPath(state, clip) == path)
        {
            const auto clipTileBounds = waveformTileCache_.visibleTileBoundsForClip(
                clipBounds(state, clip),
                visibleBounds,
                pixelsPerSecond_);
            if (clipTileBounds.empty())
            {
                const auto dirtyBounds = clipBounds(state, clip).expanded(2, 2).getIntersection(visibleBounds);
                if (!dirtyBounds.isEmpty())
                {
                    repaint(dirtyBounds);
                }
                continue;
            }
            for (const auto& tileBounds : clipTileBounds)
            {
                const auto dirtyBounds = tileBounds.expanded(2, 2).getIntersection(visibleBounds);
                if (!dirtyBounds.isEmpty())
                {
                    repaint(dirtyBounds);
                }
            }
        }
    }
}
}
#endif
