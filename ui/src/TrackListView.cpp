#include "TrackListView.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
TrackListView::TrackListView(moon::engine::TimelineFacade& timeline, moon::engine::ProjectManager& projectManager)
    : timeline_(timeline)
    , projectManager_(projectManager)
{
}

void TrackListView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkslategrey);
    g.setColour(juce::Colours::white);
    g.drawText("Tracks", getLocalBounds().removeFromTop(24), juce::Justification::centredLeft);

    const auto& tracks = projectManager_.state().tracks;
    int y = 32;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks[i];
        const auto row = rowBounds(static_cast<int>(i));
        if (track.id == projectManager_.state().uiState.selectedTrackId)
        {
            g.setColour(juce::Colours::darkorange.withAlpha(0.25f));
            g.fillRoundedRectangle(row.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
        }
        g.drawText(track.name, row.removeFromLeft(getWidth() - 80), juce::Justification::centredLeft);

        const auto muteBounds = muteButtonBounds(rowBounds(static_cast<int>(i)));
        const auto soloBounds = soloButtonBounds(rowBounds(static_cast<int>(i)));
        g.setColour(track.mute ? juce::Colours::indianred : juce::Colours::dimgrey);
        g.fillRoundedRectangle(muteBounds.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawText("M", muteBounds, juce::Justification::centred);

        g.setColour(track.solo ? juce::Colours::goldenrod : juce::Colours::dimgrey);
        g.fillRoundedRectangle(soloBounds.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawText("S", soloBounds, juce::Justification::centred);
        y += 32;
    }

    const auto addBounds = addTrackBounds();
    g.setColour(juce::Colours::seagreen);
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

juce::Rectangle<int> TrackListView::rowBounds(int rowIndex) const
{
    return {4, 30 + rowIndex * 32, getWidth() - 8, 28};
}

juce::Rectangle<int> TrackListView::muteButtonBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 64, row.getY() + 4, 24, row.getHeight() - 8};
}

juce::Rectangle<int> TrackListView::soloButtonBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 34, row.getY() + 4, 24, row.getHeight() - 8};
}

juce::Rectangle<int> TrackListView::addTrackBounds() const
{
    return {8, getHeight() - 36, getWidth() - 16, 28};
}
}
#endif
