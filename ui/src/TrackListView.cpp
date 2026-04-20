#include "TrackListView.h"

#include <algorithm>
#include <array>

#if MOON_HAS_JUCE
namespace moon::ui
{
namespace
{
juce::Colour parseTrackColour(const std::string& colorHex)
{
    if (colorHex.size() == 7 && colorHex.front() == '#')
    {
        return juce::Colour::fromString(juce::String("ff") + juce::String(colorHex.substr(1)));
    }
    return juce::Colour::fromRGB(41, 149, 255);
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

juce::String colourHexString(juce::Colour colour)
{
    return juce::String("#") + colour.toDisplayString(false).toUpperCase();
}

juce::Colour trackAccent(const moon::engine::TrackInfo& track, bool selected, int index)
{
    auto accent = track.colorHex.empty() ? defaultTrackColour(index) : parseTrackColour(track.colorHex);
    if (selected)
    {
        accent = accent.brighter(0.16f);
    }
    return accent;
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
                             std::function<void()> onTrackMixChanged,
                             std::function<bool(const std::string&, const std::string&)> onTrackRenamed,
                             std::function<bool(const std::string&)> onTrackDeleted,
                             std::function<bool(const std::string&, const std::string&)> onTrackColorChanged)
    : timeline_(timeline)
    , projectManager_(projectManager)
    , onTrackMixChanged_(std::move(onTrackMixChanged))
    , onTrackRenamed_(std::move(onTrackRenamed))
    , onTrackDeleted_(std::move(onTrackDeleted))
    , onTrackColorChanged_(std::move(onTrackColorChanged))
{
    addAndMakeVisible(renameEditor_);
    renameEditor_.setVisible(false);
    renameEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(27, 31, 36));
    renameEditor_.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(65, 71, 80));
    renameEditor_.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.95f));
    renameEditor_.setColour(juce::TextEditor::highlightColourId, juce::Colour::fromRGB(45, 150, 255).withAlpha(0.35f));
    renameEditor_.onReturnKey = [this] { commitRename(); };
    renameEditor_.onEscapeKey = [this] { cancelRename(); };
    renameEditor_.onFocusLost = [this] { commitRename(); };
}

void TrackListView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(13, 14, 18));

    const auto& tracks = projectManager_.state().tracks;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks[i];
        auto row = rowBounds(static_cast<int>(i));
        const bool isSelected = track.id == projectManager_.state().uiState.selectedTrackId;
        const bool isDropHover = track.id == dropHoverTrackId_;
        const auto accent = trackAccent(track, isSelected, static_cast<int>(i));

        g.setColour(juce::Colour::fromRGB(24, 27, 32));
        g.fillRoundedRectangle(row.toFloat(), 6.0f);

        const auto accentBounds = accentStripBounds(row);
        g.setColour(accent);
        g.fillRoundedRectangle(accentBounds.toFloat(), 3.0f);
        row.translate(5, 0);

        if (isSelected)
        {
            g.setColour(accent.withAlpha(0.18f));
            g.fillRoundedRectangle(row.toFloat(), 6.0f);
            g.setColour(juce::Colours::white);
        }

        if (isDropHover)
        {
            g.setColour(accent.withAlpha(0.18f));
            g.fillRoundedRectangle(row.toFloat(), 6.0f);
            g.setColour(accent.withAlpha(0.82f));
            g.drawRoundedRectangle(row.toFloat(), 6.0f, 1.4f);
        }

        auto content = row.reduced(9, 6);
        auto topRow = content.removeFromTop(22);

        g.setColour(juce::Colours::white.withAlpha(0.94f));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        if (renamingTrackId_ != track.id)
        {
            g.drawText(track.name, nameBounds(rowBounds(static_cast<int>(i))), juce::Justification::centredLeft);
        }

        auto muteBounds = muteButtonBounds(rowBounds(static_cast<int>(i)));
        auto soloBounds = soloButtonBounds(rowBounds(static_cast<int>(i)));

        g.setColour(track.mute ? juce::Colour::fromRGB(180, 70, 70) : juce::Colour::fromRGB(56, 60, 66));
        g.fillRoundedRectangle(muteBounds.toFloat(), 5.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("M", muteBounds, juce::Justification::centred);

        g.setColour(track.solo ? juce::Colour::fromRGB(198, 164, 52) : juce::Colour::fromRGB(56, 60, 66));
        g.fillRoundedRectangle(soloBounds.toFloat(), 5.0f);
        g.setColour(juce::Colours::white);
        g.drawText("S", soloBounds, juce::Justification::centred);

        auto statsRow = content.removeFromTop(16);
        const auto panBounds = panSliderBounds(row);

        g.setColour(juce::Colours::lightgrey.withAlpha(0.84f));
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText("Gain " + juce::String(track.gainDb, 1), statsRow.removeFromLeft(82), juce::Justification::centredLeft);
        g.drawText("PAN", statsRow.removeFromLeft(30), juce::Justification::centredLeft);

        g.setColour(juce::Colour::fromRGB(48, 53, 60));
        g.fillRoundedRectangle(panBounds.toFloat(), 6.0f);
        g.setColour(juce::Colour::fromRGB(71, 78, 90));
        g.drawRoundedRectangle(panBounds.toFloat(), 6.0f, 1.0f);

        const auto panCenter = panBounds.getX() + 8 + static_cast<int>(((track.pan + 1.0) * 0.5) * static_cast<double>(juce::jmax(1, panBounds.getWidth() - 16)));
        g.setColour(juce::Colour::fromRGB(31, 181, 235));
        g.fillEllipse(static_cast<float>(panCenter - 4), static_cast<float>(panBounds.getCentreY() - 4), 8.0f, 8.0f);

        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        g.drawText(panLabel(track.pan), panBounds.withTrimmedLeft(10), juce::Justification::centredLeft);

        const auto gainBounds = gainSliderBounds(row);
        g.setColour(juce::Colour::fromRGB(52, 56, 64));
        g.fillRoundedRectangle(gainBounds.toFloat(), 4.0f);

        const auto gainNorm = juce::jlimit(0.0, 1.0, (track.gainDb + 24.0) / 30.0);
        const auto gainFill = gainBounds.withWidth(static_cast<int>(gainBounds.getWidth() * gainNorm));
        g.setColour(juce::Colour::fromRGB(18, 172, 74));
        g.fillRoundedRectangle(gainFill.toFloat(), 4.0f);
    }
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

        if (accentStripBounds(row).expanded(3, 0).contains(event.getPosition()))
        {
            showTrackColorMenu(track, accentStripBounds(row));
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
}

void TrackListView::mouseDoubleClick(const juce::MouseEvent& event)
{
    auto& state = projectManager_.state();
    for (std::size_t i = 0; i < state.tracks.size(); ++i)
    {
        const auto row = rowBounds(static_cast<int>(i));
        if (!row.contains(event.getPosition()))
        {
            continue;
        }

        const auto& track = state.tracks[i];
        if (nameBounds(row).contains(event.getPosition()))
        {
            beginRename(track, static_cast<int>(i));
            return;
        }
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

void TrackListView::resized()
{
    if (renamingRowIndex_ >= 0)
    {
        renameEditor_.setBounds(nameBounds(rowBounds(renamingRowIndex_)).reduced(0, 1));
    }
}

void TrackListView::setDropHoverTrackId(const std::string& trackId)
{
    if (dropHoverTrackId_ == trackId)
    {
        return;
    }

    dropHoverTrackId_ = trackId;
    repaint();
}

juce::Rectangle<int> TrackListView::rowBounds(int rowIndex) const
{
    return {4, moon::ui::layout::trackRowY(rowIndex), getWidth() - 8, moon::ui::layout::kTrackRowHeight};
}

juce::Rectangle<int> TrackListView::accentStripBounds(const juce::Rectangle<int>& row) const
{
    return {row.getX(), row.getY(), 5, row.getHeight()};
}

juce::Rectangle<int> TrackListView::muteButtonBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 56, row.getY() + 6, 20, 20};
}

juce::Rectangle<int> TrackListView::soloButtonBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 31, row.getY() + 6, 20, 20};
}

juce::Rectangle<int> TrackListView::nameBounds(const juce::Rectangle<int>& row) const
{
    return {row.getX() + 14, row.getY() + 4, row.getWidth() - 96, 24};
}

juce::Rectangle<int> TrackListView::gainSliderBounds(const juce::Rectangle<int>& row) const
{
    return {row.getX() + 9, row.getBottom() - 14, row.getWidth() - 18, 8};
}

juce::Rectangle<int> TrackListView::panSliderBounds(const juce::Rectangle<int>& row) const
{
    return {row.getRight() - 66, row.getY() + 34, 52, 12};
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

void TrackListView::beginRename(const moon::engine::TrackInfo& track, int rowIndex)
{
    renamingTrackId_ = track.id;
    renameOriginalName_ = track.name;
    renamingRowIndex_ = rowIndex;
    renameEditor_.setText(track.name, juce::dontSendNotification);
    renameEditor_.setBounds(nameBounds(rowBounds(rowIndex)).reduced(0, 1));
    renameEditor_.setVisible(true);
    renameEditor_.grabKeyboardFocus();
    renameEditor_.selectAll();
    repaint();
}

void TrackListView::commitRename()
{
    if (renamingTrackId_.empty())
    {
        return;
    }

    auto replacement = renameEditor_.getText().trim().toStdString();
    if (replacement.empty())
    {
        replacement = renameOriginalName_;
    }

    if (onTrackRenamed_)
    {
        onTrackRenamed_(renamingTrackId_, replacement);
    }

    renameEditor_.setVisible(false);
    renamingTrackId_.clear();
    renameOriginalName_.clear();
    renamingRowIndex_ = -1;
    repaint();
}

void TrackListView::cancelRename()
{
    if (renamingTrackId_.empty())
    {
        return;
    }

    renameEditor_.setVisible(false);
    renamingTrackId_.clear();
    renameOriginalName_.clear();
    renamingRowIndex_ = -1;
    repaint();
}

void TrackListView::showTrackColorMenu(const moon::engine::TrackInfo& track, const juce::Rectangle<int>& swatchBounds)
{
    struct PaletteEntry
    {
        int id;
        const char* label;
        juce::Colour colour;
    };

    static const std::array<PaletteEntry, 7> palette{{
        {1, "Blue", juce::Colour::fromRGB(45, 150, 255)},
        {2, "Magenta", juce::Colour::fromRGB(255, 43, 138)},
        {3, "Green", juce::Colour::fromRGB(24, 196, 88)},
        {4, "Amber", juce::Colour::fromRGB(235, 196, 20)},
        {5, "Purple", juce::Colour::fromRGB(140, 102, 255)},
        {6, "Orange", juce::Colour::fromRGB(255, 135, 64)},
        {7, "Teal", juce::Colour::fromRGB(25, 194, 202)},
    }};

    juce::PopupMenu menu;
    for (const auto& entry : palette)
    {
        menu.addItem(entry.id, entry.label);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(swatchBounds.translated(getScreenX(), getScreenY())),
        [this, trackId = track.id](int result)
        {
            if (result <= 0 || !onTrackColorChanged_)
            {
                return;
            }

            static const std::array<juce::Colour, 7> colours{
                juce::Colour::fromRGB(45, 150, 255),
                juce::Colour::fromRGB(255, 43, 138),
                juce::Colour::fromRGB(24, 196, 88),
                juce::Colour::fromRGB(235, 196, 20),
                juce::Colour::fromRGB(140, 102, 255),
                juce::Colour::fromRGB(255, 135, 64),
                juce::Colour::fromRGB(25, 194, 202)};
            onTrackColorChanged_(trackId, colourHexString(colours[static_cast<std::size_t>(result - 1)]).toStdString());
            repaint();
        });
}
}
#endif

