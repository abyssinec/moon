#include "TrackListView.h"

#include <algorithm>

#if MOON_HAS_JUCE
namespace moon::ui
{
namespace
{
std::string lowercase(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
    return value;
}

juce::Colour trackAccent(const moon::engine::TrackInfo& track, bool selected)
{
    if (selected)
    {
        return juce::Colour::fromRGB(45, 150, 255);
    }

    const auto name = lowercase(track.name);
    if (name.find("vocal") != std::string::npos)
    {
        return juce::Colour::fromRGB(40, 140, 255);
    }
    if (name.find("drum") != std::string::npos || name.find("bass") != std::string::npos || name.find("guitar") != std::string::npos)
    {
        return juce::Colour::fromRGB(18, 196, 88);
    }
    if (name.find("key") != std::string::npos || name.find("synth") != std::string::npos || name.find("pad") != std::string::npos)
    {
        return juce::Colour::fromRGB(235, 196, 20);
    }
    return juce::Colour::fromRGB(255, 30, 120);
}

std::string panLabel(double pan)
{
    if (pan < -0.1)
    {
        return "L";
    }
    if (pan > 0.1)
    {
        return "R";
    }
    return "C";
}
}

TrackListView::TrackListView(moon::engine::TimelineFacade& timeline,
                             moon::engine::ProjectManager& projectManager,
                             std::function<void()> onTrackMixChanged)
    : timeline_(timeline)
    , projectManager_(projectManager)
    , onTrackMixChanged_(std::move(onTrackMixChanged))
{
}

void TrackListView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(13, 14, 18));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText("Tracks", getLocalBounds().removeFromTop(22).reduced(8, 0), juce::Justification::centredLeft);

    const auto& tracks = projectManager_.state().tracks;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks[i];
        auto row = rowBounds(static_cast<int>(i));
        const bool isSelected = track.id == projectManager_.state().uiState.selectedTrackId;
        const auto accent = trackAccent(track, isSelected);
        g.setColour(juce::Colour::fromRGB(22, 24, 29));
        g.fillRoundedRectangle(row.toFloat(), 6.0f);
        g.setColour(accent);
        g.fillRoundedRectangle(row.removeFromLeft(5).toFloat(), 3.0f);
        row.translate(5, 0);
        if (isSelected)
        {
            g.setColour(accent.withAlpha(0.18f));
            g.fillRoundedRectangle(row.toFloat(), 6.0f);
            g.setColour(juce::Colours::white);
        }
        auto content = row.reduced(8, 5);
        auto topRow = content.removeFromTop(18);
        auto numberBox = topRow.removeFromLeft(17);
        g.setColour(accent.withAlpha(0.95f));
        g.fillRoundedRectangle(numberBox.toFloat(), 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.9f));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(juce::String(static_cast<int>(i) + 1), numberBox, juce::Justification::centred);

        topRow.removeFromLeft(11);
        g.setColour(juce::Colours::white.withAlpha(0.94f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(track.name, topRow.removeFromLeft(70), juce::Justification::centredLeft);

        auto muteBounds = muteButtonBounds(rowBounds(static_cast<int>(i)));
        auto soloBounds = soloButtonBounds(rowBounds(static_cast<int>(i)));
        g.setColour(track.mute ? juce::Colour::fromRGB(180, 70, 70) : juce::Colour::fromRGB(56, 60, 66));
        g.fillRoundedRectangle(muteBounds.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawText("M", muteBounds, juce::Justification::centred);

        g.setColour(track.solo ? juce::Colour::fromRGB(198, 164, 52) : juce::Colour::fromRGB(56, 60, 66));
        g.fillRoundedRectangle(soloBounds.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawText("S", soloBounds, juce::Justification::centred);

        auto statsRow = content.removeFromTop(13);
        const auto panBounds = panSliderBounds(row);
        g.setColour(juce::Colours::lightgrey.withAlpha(0.72f));
        g.setFont(juce::FontOptions(8.5f));
        g.drawText("Vol " + juce::String(track.gainDb, 1), statsRow.removeFromLeft(58), juce::Justification::centredLeft);
        g.drawText("PAN", statsRow.removeFromLeft(20), juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGB(48, 53, 60));
        g.fillRoundedRectangle(panBounds.toFloat(), 5.0f);
        g.setColour(juce::Colour::fromRGB(71, 78, 90));
        g.drawRoundedRectangle(panBounds.toFloat(), 5.0f, 1.0f);
        const auto panCenter = panBounds.getX() + 6 + static_cast<int>(((track.pan + 1.0) * 0.5) * static_cast<double>(juce::jmax(1, panBounds.getWidth() - 12)));
        g.setColour(juce::Colour::fromRGB(31, 181, 235));
        g.fillEllipse(static_cast<float>(panCenter - 3), static_cast<float>(panBounds.getCentreY() - 3), 6.0f, 6.0f);
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(panLabel(track.pan), panBounds.withTrimmedLeft(8), juce::Justification::centredLeft);

        const auto gainBounds = gainSliderBounds(row);
        g.setColour(juce::Colour::fromRGB(52, 56, 64));
        g.fillRoundedRectangle(gainBounds.toFloat(), 3.0f);

        const auto gainNorm = juce::jlimit(0.0, 1.0, (track.gainDb + 24.0) / 30.0);
        const auto gainFill = gainBounds.withWidth(static_cast<int>(gainBounds.getWidth() * gainNorm));
        g.setColour(juce::Colour::fromRGB(18, 172, 74));
        g.fillRoundedRectangle(gainFill.toFloat(), 3.0f);
    }

    const auto addBounds = addTrackBounds();
    g.setColour(juce::Colour::fromRGB(18, 110, 58));
    g.fillRoundedRectangle(addBounds.toFloat(), 5.0f);
    g.setColour(juce::Colours::white);
    g.drawText("+ Add Track", addBounds, juce::Justification::centred);
}

void TrackListView::mouseDown(const juce::MouseEvent& event)
{
    auto& state = projectManager_.state();
    for (std::size_t i = 0; i < state.tracks.size(); ++i)
    {
        auto row = rowBounds(static_cast<int>(i));
        const auto& track = state.tracks[i];
        if (!row.contains(event.getPosition()))
        {
            continue;
        }

        if (muteButtonBounds(row).contains(event.getPosition()))
        {
            if (timeline_.toggleTrackMute(state, track.id))
            {
                projectManager_.saveProject();
            }
            repaint();
            return;
        }

        if (soloButtonBounds(row).contains(event.getPosition()))
        {
            if (timeline_.toggleTrackSolo(state, track.id))
            {
                projectManager_.saveProject();
            }
            repaint();
            return;
        }

        if (gainSliderBounds(row).expanded(0, 6).contains(event.getPosition()))
        {
            applyTrackGainFromPosition(state.tracks[i], event.x, gainSliderBounds(row));
            if (onTrackMixChanged_)
            {
                onTrackMixChanged_();
            }
            dragMode_ = DragMode::Gain;
            dragTrackId_ = track.id;
            repaint();
            return;
        }

        if (panSliderBounds(row).expanded(6, 8).contains(event.getPosition()))
        {
            applyTrackPanFromPosition(state.tracks[i], event.x, panSliderBounds(row));
            if (onTrackMixChanged_)
            {
                onTrackMixChanged_();
            }
            dragMode_ = DragMode::Pan;
            dragTrackId_ = track.id;
            repaint();
            return;
        }

        timeline_.selectTrack(state, track.id);
        repaint();
        return;
    }

    if (addTrackBounds().contains(event.getPosition()))
    {
        const auto trackName = "Track " + std::to_string(state.tracks.size() + 1);
        timeline_.ensureTrack(state, trackName);
        projectManager_.saveProject();
        repaint();
    }
}

void TrackListView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::None || dragTrackId_.empty())
    {
        return;
    }

    auto& state = projectManager_.state();
    for (std::size_t i = 0; i < state.tracks.size(); ++i)
    {
        auto& track = state.tracks[i];
        if (track.id != dragTrackId_)
        {
            continue;
        }

        const auto row = rowBounds(static_cast<int>(i));
        if (dragMode_ == DragMode::Gain)
        {
            applyTrackGainFromPosition(track, event.x, gainSliderBounds(row));
        }
        else if (dragMode_ == DragMode::Pan)
        {
            applyTrackPanFromPosition(track, event.x, panSliderBounds(row));
        }
        if (onTrackMixChanged_)
        {
            onTrackMixChanged_();
        }
        repaint();
        return;
    }
}

void TrackListView::mouseUp(const juce::MouseEvent&)
{
    if (dragMode_ != DragMode::None)
    {
        projectManager_.saveProject();
    }

    dragMode_ = DragMode::None;
    dragTrackId_.clear();
}

juce::Rectangle<int> TrackListView::rowBounds(int rowIndex) const
{
    return {4, 28 + rowIndex * 60, getWidth() - 8, 54};
}

juce::Rectangle<int> TrackListView::muteButtonBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 48, row.getY() + 6, 17, 17};
}

juce::Rectangle<int> TrackListView::soloButtonBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 27, row.getY() + 6, 17, 17};
}

juce::Rectangle<int> TrackListView::gainSliderBounds(const juce::Rectangle<int>& row) const
{
    return {row.getX() + 8, row.getBottom() - 12, row.getWidth() - 16, 6};
}

juce::Rectangle<int> TrackListView::panSliderBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 58, row.getY() + 23, 50, 10};
}

juce::Rectangle<int> TrackListView::addTrackBounds() const
{
    return {8, getHeight() - 42, getWidth() - 16, 32};
}

void TrackListView::applyTrackGainFromPosition(moon::engine::TrackInfo& track, int x, const juce::Rectangle<int>& bounds)
{
    const auto ratio = juce::jlimit(0.0, 1.0, static_cast<double>(x - bounds.getX()) / static_cast<double>(juce::jmax(1, bounds.getWidth())));
    track.gainDb = -24.0 + ratio * 30.0;
}

void TrackListView::applyTrackPanFromPosition(moon::engine::TrackInfo& track, int x, const juce::Rectangle<int>& bounds)
{
    const auto ratio = juce::jlimit(0.0, 1.0, static_cast<double>(x - bounds.getX()) / static_cast<double>(juce::jmax(1, bounds.getWidth())));
    track.pan = -1.0 + ratio * 2.0;
}
}
#endif

