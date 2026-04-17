#include "InspectorPanel.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
namespace
{
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
}

InspectorPanel::InspectorPanel(moon::engine::ProjectManager& projectManager,
                               moon::engine::TaskManager& taskManager,
                               moon::engine::Logger& logger,
                               std::function<std::string()> promptProvider,
                               std::function<void(double)> onGainChanged,
                               std::function<void(double)> onFadeInChanged,
                               std::function<void(double)> onFadeOutChanged,
                               std::function<void(double)> onTrimLeft,
                               std::function<void(double)> onTrimRight,
                               std::function<void()> onClearRegion,
                               std::function<void()> onSplitClip,
                               std::function<void()> onActivateTake,
                               std::function<void()> onDuplicateClip,
                               std::function<void()> onDeleteClip,
                               std::function<void(double)> onCrossfadePrevious,
                               std::function<void(double)> onCrossfadeNext,
                               std::function<void(const std::string&)> onRewrite,
                               std::function<void(const std::string&)> onAddLayer,
                               std::function<void()> onStems)
    : projectManager_(projectManager)
    , taskManager_(taskManager)
    , logger_(logger)
    , promptProvider_(std::move(promptProvider))
    , onGainChanged_(std::move(onGainChanged))
    , onFadeInChanged_(std::move(onFadeInChanged))
    , onFadeOutChanged_(std::move(onFadeOutChanged))
    , onTrimLeft_(std::move(onTrimLeft))
    , onTrimRight_(std::move(onTrimRight))
    , onClearRegion_(std::move(onClearRegion))
    , onSplitClip_(std::move(onSplitClip))
    , onActivateTake_(std::move(onActivateTake))
    , onDuplicateClip_(std::move(onDuplicateClip))
    , onDeleteClip_(std::move(onDeleteClip))
    , onCrossfadePrevious_(std::move(onCrossfadePrevious))
    , onCrossfadeNext_(std::move(onCrossfadeNext))
    , onRewrite_(std::move(onRewrite))
    , onAddLayer_(std::move(onAddLayer))
    , onStems_(std::move(onStems))
{
    addAndMakeVisible(title_);
    addAndMakeVisible(selectionSummary_);
    addAndMakeVisible(clipDetailsLabel_);
    addAndMakeVisible(gainLabel_);
    addAndMakeVisible(fadeLabel_);
    addAndMakeVisible(crossfadeLabel_);
    addAndMakeVisible(promptEditor_);
    addAndMakeVisible(strengthSlider_);
    addAndMakeVisible(gainSlider_);
    addAndMakeVisible(fadeInSlider_);
    addAndMakeVisible(fadeOutSlider_);
    addAndMakeVisible(crossfadeSlider_);
    addAndMakeVisible(preserveTiming_);
    addAndMakeVisible(preserveMelody_);
    addAndMakeVisible(trimLeftButton_);
    addAndMakeVisible(trimRightButton_);
    addAndMakeVisible(extendLeftButton_);
    addAndMakeVisible(extendRightButton_);
    addAndMakeVisible(clearRegionButton_);
    addAndMakeVisible(splitClipButton_);
    addAndMakeVisible(activateTakeButton_);
    addAndMakeVisible(duplicateClipButton_);
    addAndMakeVisible(deleteClipButton_);
    addAndMakeVisible(crossfadePreviousButton_);
    addAndMakeVisible(crossfadeNextButton_);
    addAndMakeVisible(stemsButton_);
    addAndMakeVisible(rewriteButton_);
    addAndMakeVisible(addLayerButton_);

    auto styleLabel = [](juce::Label& label, float fontSize, bool bold = false)
    {
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(bold ? 0.95f : 0.78f));
        label.setFont(juce::FontOptions(fontSize, bold ? juce::Font::bold : juce::Font::plain));
    };
    styleLabel(title_, 15.0f, true);
    styleLabel(selectionSummary_, 13.0f, true);
    styleLabel(clipDetailsLabel_, 12.0f, false);
    styleLabel(gainLabel_, 12.0f, true);
    styleLabel(fadeLabel_, 12.0f, true);
    styleLabel(crossfadeLabel_, 12.0f, true);
    clipDetailsLabel_.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(26, 29, 34));
    clipDetailsLabel_.setJustificationType(juce::Justification::topLeft);

    auto styleSlider = [](juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 22);
        slider.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGB(42, 46, 53));
        slider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(43, 169, 237));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(101, 217, 255));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB(28, 31, 36));
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.92f));
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB(54, 58, 64));
    };
    styleSlider(strengthSlider_);
    styleSlider(gainSlider_);
    styleSlider(fadeInSlider_);
    styleSlider(fadeOutSlider_);
    styleSlider(crossfadeSlider_);

    auto styleButton = [](juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(33, 36, 42));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(43, 169, 237));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    };
    styleButton(trimLeftButton_);
    styleButton(trimRightButton_);
    styleButton(extendLeftButton_);
    styleButton(extendRightButton_);
    styleButton(clearRegionButton_);
    styleButton(splitClipButton_);
    styleButton(activateTakeButton_);
    styleButton(duplicateClipButton_);
    styleButton(deleteClipButton_);
    styleButton(crossfadePreviousButton_);
    styleButton(crossfadeNextButton_);
    styleButton(stemsButton_);
    styleButton(rewriteButton_);
    styleButton(addLayerButton_);

    promptEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(24, 27, 32));
    promptEditor_.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(56, 60, 66));
    promptEditor_.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.9f));
    promptEditor_.setColour(juce::CaretComponent::caretColourId, juce::Colours::white);
    promptEditor_.setMultiLine(true);
    promptEditor_.setReturnKeyStartsNewLine(true);

    title_.setText("Inspector", juce::dontSendNotification);
    selectionSummary_.setText("No clip selected", juce::dontSendNotification);
    clipDetailsLabel_.setText("Select a clip to inspect source path and AI actions", juce::dontSendNotification);
    gainLabel_.setText("Clip gain", juce::dontSendNotification);
    fadeLabel_.setText("Clip fades", juce::dontSendNotification);
    crossfadeLabel_.setText("Crossfade overlap: 0.20s", juce::dontSendNotification);
    promptEditor_.setText("dark cinematic remix, preserve rhythm");
    strengthSlider_.setRange(0.0, 1.0, 0.01);
    strengthSlider_.setValue(0.55);
    gainSlider_.setRange(0.0, 2.0, 0.01);
    gainSlider_.setValue(1.0);
    fadeInSlider_.setRange(0.0, 2.0, 0.01);
    fadeOutSlider_.setRange(0.0, 2.0, 0.01);
    crossfadeSlider_.setRange(0.05, 2.0, 0.01);
    crossfadeSlider_.setValue(0.20);
    gainSlider_.onValueChange = [this]
    {
        if (!refreshingControls_ && onGainChanged_)
        {
            onGainChanged_(gainSlider_.getValue());
        }
    };
    fadeInSlider_.onValueChange = [this]
    {
        if (!refreshingControls_ && onFadeInChanged_)
        {
            onFadeInChanged_(fadeInSlider_.getValue());
        }
    };
    fadeOutSlider_.onValueChange = [this]
    {
        if (!refreshingControls_ && onFadeOutChanged_)
        {
            onFadeOutChanged_(fadeOutSlider_.getValue());
        }
    };

    trimLeftButton_.onClick = [this]
    {
        if (onTrimLeft_)
        {
            onTrimLeft_(0.1);
        }
    };
    trimRightButton_.onClick = [this]
    {
        if (onTrimRight_)
        {
            onTrimRight_(-0.1);
        }
    };
    extendLeftButton_.onClick = [this]
    {
        if (onTrimLeft_)
        {
            onTrimLeft_(-0.1);
        }
    };
    extendRightButton_.onClick = [this]
    {
        if (onTrimRight_)
        {
            onTrimRight_(0.1);
        }
    };
    clearRegionButton_.onClick = [this]
    {
        if (onClearRegion_)
        {
            onClearRegion_();
        }
    };
    splitClipButton_.onClick = [this]
    {
        if (onSplitClip_)
        {
            onSplitClip_();
        }
    };
    activateTakeButton_.onClick = [this]
    {
        if (onActivateTake_)
        {
            onActivateTake_();
        }
    };
    duplicateClipButton_.onClick = [this]
    {
        if (onDuplicateClip_)
        {
            onDuplicateClip_();
        }
    };
    deleteClipButton_.onClick = [this]
    {
        if (onDeleteClip_)
        {
            onDeleteClip_();
        }
    };
    crossfadePreviousButton_.onClick = [this]
    {
        if (onCrossfadePrevious_)
        {
            onCrossfadePrevious_(crossfadeSlider_.getValue());
        }
    };
    crossfadeNextButton_.onClick = [this]
    {
        if (onCrossfadeNext_)
        {
            onCrossfadeNext_(crossfadeSlider_.getValue());
        }
    };

    stemsButton_.onClick = [this]
    {
        if (onStems_)
        {
            onStems_();
        }
    };
    rewriteButton_.onClick = [this]
    {
        if (onRewrite_)
        {
            onRewrite_(promptEditor_.getText().toStdString());
        }
    };
    addLayerButton_.onClick = [this]
    {
        if (onAddLayer_)
        {
            onAddLayer_(promptEditor_.getText().toStdString());
        }
    };
}

void InspectorPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(15, 17, 21));
    auto area = getLocalBounds().reduced(8);
    g.setColour(juce::Colour::fromRGB(22, 24, 29));
    g.fillRoundedRectangle(area.toFloat(), 14.0f);
    g.setColour(juce::Colour::fromRGB(42, 46, 52));
    g.drawRoundedRectangle(area.toFloat(), 14.0f, 1.0f);

    auto headerArea = area.removeFromTop(56);
    g.setColour(juce::Colour::fromRGB(18, 20, 25));
    g.fillRoundedRectangle(headerArea.toFloat(), 12.0f);

    auto detailsCard = area.removeFromTop(176);
    g.setColour(juce::Colour::fromRGB(19, 22, 27));
    g.fillRoundedRectangle(detailsCard.toFloat(), 12.0f);

    auto controlsCard = area.removeFromTop(270);
    g.setColour(juce::Colour::fromRGB(19, 22, 27));
    g.fillRoundedRectangle(controlsCard.toFloat(), 12.0f);

    auto aiCard = area;
    g.setColour(juce::Colour::fromRGB(19, 22, 27));
    g.fillRoundedRectangle(aiCard.toFloat(), 12.0f);

    const auto& state = projectManager_.state();
    const bool hasSelectedClip = !state.uiState.selectedClipId.empty();
    const bool hasSelectedRegion = state.uiState.hasSelectedRegion;
    std::string summary = "No clip selected";
    if (hasSelectedClip)
    {
        summary = "Clip: " + state.uiState.selectedClipId;
        if (hasSelectedRegion)
        {
            summary += " | Region: "
                + juce::String(state.uiState.selectedRegionStartSec, 2).toStdString()
                + " - "
                + juce::String(state.uiState.selectedRegionEndSec, 2).toStdString()
                + " sec";
        }
    }
    selectionSummary_.setText(summary, juce::dontSendNotification);

    std::string details = "No asset loaded";
    double gain = 1.0;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    for (const auto& clip : state.clips)
    {
        if (clip.id != state.uiState.selectedClipId)
        {
            continue;
        }

        details = "Start: "
            + juce::String(clip.startSec, 2).toStdString()
            + "s  End: "
            + juce::String(clip.startSec + clip.durationSec, 2).toStdString()
            + "s  Gain: "
            + juce::String(clip.gain, 2).toStdString()
            + "\nFade In: "
            + juce::String(clip.fadeInSec, 2).toStdString()
            + "s  Fade Out: "
            + juce::String(clip.fadeOutSec, 2).toStdString()
            + "s";
        if (!clip.takeGroupId.empty())
        {
            details += "\nTake Group: " + clip.takeGroupId;
            details += "\nTake State: ";
            details += (clip.activeTake ? "active" : "inactive");
        }

        std::vector<std::string> overlapLines;
        for (const auto& other : state.clips)
        {
            if (other.id == clip.id || other.trackId != clip.trackId || !other.activeTake || !trackIsAudible(state, other.trackId))
            {
                continue;
            }

            const auto overlapStart = std::max(clip.startSec, other.startSec);
            const auto overlapEnd = std::min(clip.startSec + clip.durationSec, other.startSec + other.durationSec);
            if (overlapEnd <= overlapStart)
            {
                continue;
            }

            overlapLines.push_back(
                other.id + " (" + juce::String(overlapEnd - overlapStart, 2).toStdString() + "s)");
        }

        if (overlapLines.empty())
        {
            details += "\nAuto Crossfade: none";
        }
        else
        {
            details += "\nAuto Crossfade: " + overlapLines.front();
            for (std::size_t overlapIndex = 1; overlapIndex < overlapLines.size(); ++overlapIndex)
            {
                details += ", " + overlapLines[overlapIndex];
            }
        }
        gain = clip.gain;
        fadeIn = clip.fadeInSec;
        fadeOut = clip.fadeOutSec;

        if (const auto sourceIt = state.sourceAssets.find(clip.assetId); sourceIt != state.sourceAssets.end())
        {
            details += "\nSource: " + sourceIt->second.path;
        }
        else if (const auto generatedIt = state.generatedAssets.find(clip.assetId); generatedIt != state.generatedAssets.end())
        {
            details += "\nGenerated: " + generatedIt->second.path;
            details += "\nModel: " + generatedIt->second.modelName;
        }
        break;
    }
    clipDetailsLabel_.setText(details, juce::dontSendNotification);
    refreshingControls_ = true;
    gainSlider_.setValue(gain, juce::dontSendNotification);
    fadeInSlider_.setValue(fadeIn, juce::dontSendNotification);
    fadeOutSlider_.setValue(fadeOut, juce::dontSendNotification);
    refreshingControls_ = false;

    gainSlider_.setEnabled(hasSelectedClip);
    fadeInSlider_.setEnabled(hasSelectedClip);
    fadeOutSlider_.setEnabled(hasSelectedClip);
    crossfadeSlider_.setEnabled(hasSelectedClip);
    trimLeftButton_.setEnabled(hasSelectedClip);
    trimRightButton_.setEnabled(hasSelectedClip);
    extendLeftButton_.setEnabled(hasSelectedClip);
    extendRightButton_.setEnabled(hasSelectedClip);
    splitClipButton_.setEnabled(hasSelectedClip);
    duplicateClipButton_.setEnabled(hasSelectedClip);
    deleteClipButton_.setEnabled(hasSelectedClip);
    crossfadePreviousButton_.setEnabled(hasSelectedClip);
    crossfadeNextButton_.setEnabled(hasSelectedClip);
    stemsButton_.setEnabled(hasSelectedClip);
    activateTakeButton_.setEnabled(hasSelectedClip);
    clearRegionButton_.setEnabled(hasSelectedRegion);
    rewriteButton_.setEnabled(hasSelectedClip && hasSelectedRegion);
    addLayerButton_.setEnabled(hasSelectedClip && hasSelectedRegion);

    gainLabel_.setText("Clip gain: " + juce::String(gain, 2), juce::dontSendNotification);
    fadeLabel_.setText(
        "Fade In: " + juce::String(fadeIn, 2) + "s | Fade Out: " + juce::String(fadeOut, 2) + "s",
        juce::dontSendNotification);
    crossfadeLabel_.setText(
        "Crossfade overlap: " + juce::String(crossfadeSlider_.getValue(), 2) + "s",
        juce::dontSendNotification);
}

void InspectorPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    title_.setBounds(area.removeFromTop(30).reduced(12, 2));
    selectionSummary_.setBounds(area.removeFromTop(46).reduced(12, 4));
    clipDetailsLabel_.setBounds(area.removeFromTop(142).reduced(8, 8));
    gainLabel_.setBounds(area.removeFromTop(24).reduced(6, 0));
    gainSlider_.setBounds(area.removeFromTop(38).reduced(4, 0));
    fadeLabel_.setBounds(area.removeFromTop(24).reduced(6, 0));
    fadeInSlider_.setBounds(area.removeFromTop(32).reduced(4, 0));
    fadeOutSlider_.setBounds(area.removeFromTop(32).reduced(4, 0));
    crossfadeLabel_.setBounds(area.removeFromTop(24).reduced(6, 0));
    crossfadeSlider_.setBounds(area.removeFromTop(32).reduced(4, 0));
    auto trimRowOne = area.removeFromTop(36);
    trimLeftButton_.setBounds(trimRowOne.removeFromLeft(trimRowOne.getWidth() / 2).reduced(2));
    trimRightButton_.setBounds(trimRowOne.reduced(2));
    auto trimRowTwo = area.removeFromTop(36);
    extendLeftButton_.setBounds(trimRowTwo.removeFromLeft(trimRowTwo.getWidth() / 2).reduced(2));
    extendRightButton_.setBounds(trimRowTwo.reduced(2));
    clearRegionButton_.setBounds(area.removeFromTop(36).reduced(2));
    auto clipActionsTop = area.removeFromTop(36);
    splitClipButton_.setBounds(clipActionsTop.removeFromLeft(clipActionsTop.getWidth() / 2).reduced(2));
    activateTakeButton_.setBounds(clipActionsTop.reduced(2));
    auto clipActionsBottom = area.removeFromTop(36);
    duplicateClipButton_.setBounds(clipActionsBottom.removeFromLeft(clipActionsBottom.getWidth() / 2).reduced(2));
    deleteClipButton_.setBounds(clipActionsBottom.reduced(2));
    auto crossfadeRow = area.removeFromTop(36);
    crossfadePreviousButton_.setBounds(crossfadeRow.removeFromLeft(crossfadeRow.getWidth() / 2).reduced(2));
    crossfadeNextButton_.setBounds(crossfadeRow.reduced(2));
    promptEditor_.setBounds(area.removeFromTop(118).reduced(2));
    strengthSlider_.setBounds(area.removeFromTop(42).reduced(4, 0));
    preserveTiming_.setBounds(area.removeFromTop(26));
    preserveMelody_.setBounds(area.removeFromTop(26));
    stemsButton_.setBounds(area.removeFromTop(36).reduced(0, 2));
    rewriteButton_.setBounds(area.removeFromTop(36).reduced(0, 2));
    addLayerButton_.setBounds(area.removeFromTop(36).reduced(0, 2));
}
}
#endif
