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

    title_.setText("Inspector / AI Tools", juce::dontSendNotification);
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
    g.fillAll(juce::Colours::darkslategrey.darker(0.4f));

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
    title_.setBounds(area.removeFromTop(24));
    selectionSummary_.setBounds(area.removeFromTop(48));
    clipDetailsLabel_.setBounds(area.removeFromTop(122));
    gainLabel_.setBounds(area.removeFromTop(22));
    gainSlider_.setBounds(area.removeFromTop(36));
    fadeLabel_.setBounds(area.removeFromTop(22));
    fadeInSlider_.setBounds(area.removeFromTop(30));
    fadeOutSlider_.setBounds(area.removeFromTop(30));
    crossfadeLabel_.setBounds(area.removeFromTop(22));
    crossfadeSlider_.setBounds(area.removeFromTop(30));
    auto trimRowOne = area.removeFromTop(32);
    trimLeftButton_.setBounds(trimRowOne.removeFromLeft(trimRowOne.getWidth() / 2).reduced(2));
    trimRightButton_.setBounds(trimRowOne.reduced(2));
    auto trimRowTwo = area.removeFromTop(32);
    extendLeftButton_.setBounds(trimRowTwo.removeFromLeft(trimRowTwo.getWidth() / 2).reduced(2));
    extendRightButton_.setBounds(trimRowTwo.reduced(2));
    clearRegionButton_.setBounds(area.removeFromTop(32).reduced(2));
    auto clipActions = area.removeFromTop(32);
    splitClipButton_.setBounds(clipActions.removeFromLeft(clipActions.getWidth() / 4).reduced(2));
    activateTakeButton_.setBounds(clipActions.removeFromLeft(clipActions.getWidth() / 3).reduced(2));
    duplicateClipButton_.setBounds(clipActions.removeFromLeft(clipActions.getWidth() / 2).reduced(2));
    deleteClipButton_.setBounds(clipActions.reduced(2));
    auto crossfadeRow = area.removeFromTop(32);
    crossfadePreviousButton_.setBounds(crossfadeRow.removeFromLeft(crossfadeRow.getWidth() / 2).reduced(2));
    crossfadeNextButton_.setBounds(crossfadeRow.reduced(2));
    promptEditor_.setBounds(area.removeFromTop(100));
    strengthSlider_.setBounds(area.removeFromTop(40));
    preserveTiming_.setBounds(area.removeFromTop(24));
    preserveMelody_.setBounds(area.removeFromTop(24));
    stemsButton_.setBounds(area.removeFromTop(32).reduced(0, 2));
    rewriteButton_.setBounds(area.removeFromTop(32).reduced(0, 2));
    addLayerButton_.setBounds(area.removeFromTop(32).reduced(0, 2));
}
}
#endif
