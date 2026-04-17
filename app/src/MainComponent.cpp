#include "MainComponent.h"

#if MOON_HAS_JUCE
namespace moon::app
{
MainComponent::MainComponent(AppController& controller)
    : controller_(controller)
    , transportBar_(
          controller.transport(),
          controller.logger(),
          [&controller]() { controller.playTransport(); },
          [&controller]() { controller.pauseTransport(); },
          [&controller]() { controller.stopTransport(); },
          [&controller]() { return controller.backendStatusSummary(); })
    , trackListView_(controller.timeline(), controller.projectManager())
    , timelineView_(
          controller.timeline(),
          controller.projectManager(),
          controller.waveformService(),
          controller.transport(),
          [&controller](double timelineSec)
          {
              controller.seekTimelinePlayhead(timelineSec);
          },
          [&controller]()
          {
              controller.beginInteractiveTimelineEdit();
          },
          [&controller](bool changed)
          {
              controller.finishInteractiveTimelineEdit(changed, true);
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
    audioSourcePlayer_ = std::make_unique<juce::AudioSourcePlayer>();
    audioSourcePlayer_->setSource(controller.transport().audioSource());
    audioDeviceManager_.initialiseWithDefaultDevices(0, 2);
    audioDeviceManager_.addAudioCallback(audioSourcePlayer_.get());

    addAndMakeVisible(newProjectButton_);
    addAndMakeVisible(openProjectButton_);
    addAndMakeVisible(saveProjectButton_);
    addAndMakeVisible(importAudioButton_);
    addAndMakeVisible(refreshBackendButton_);
    addAndMakeVisible(rebuildPreviewButton_);
    addAndMakeVisible(settingsButton_);
    addAndMakeVisible(projectStatusLabel_);
    addAndMakeVisible(exportMixButton_);
    addAndMakeVisible(exportRegionButton_);
    addAndMakeVisible(exportStemsButton_);
    addAndMakeVisible(startupNoticeLabel_);
    addAndMakeVisible(dismissStartupNoticeButton_);
    addAndMakeVisible(transportBar_);
    addAndMakeVisible(trackListView_);
    addAndMakeVisible(timelineView_);
    addAndMakeVisible(inspectorPanel_);
    addAndMakeVisible(taskPanel_);

    newProjectButton_.onClick = [this] { createNewProject(); };
    openProjectButton_.onClick = [this] { openExistingProject(); };
    saveProjectButton_.onClick = [this] { saveCurrentProject(); };
    importAudioButton_.onClick = [this] { importAudioFile(); };
    refreshBackendButton_.onClick = [this] { refreshBackendConnection(); };
    rebuildPreviewButton_.onClick = [this] { rebuildPreviewCache(); };
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
    projectStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    projectStatusLabel_.setInterceptsMouseClicks(false, false);
    refreshProjectStatusLabel();
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
    startTimerHz(4);
}

MainComponent::~MainComponent()
{
    if (audioSourcePlayer_ != nullptr)
    {
        audioSourcePlayer_->setSource(nullptr);
        audioDeviceManager_.removeAudioCallback(audioSourcePlayer_.get());
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto projectBar = area.removeFromTop(40);
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
    auto transport = area.removeFromTop(48);
    auto taskArea = area.removeFromBottom(180);
    auto inspector = area.removeFromRight(280);
    auto tracks = area.removeFromLeft(180);

    newProjectButton_.setBounds(projectBar.removeFromLeft(90).reduced(4));
    openProjectButton_.setBounds(projectBar.removeFromLeft(90).reduced(4));
    saveProjectButton_.setBounds(projectBar.removeFromLeft(90).reduced(4));
    importAudioButton_.setBounds(projectBar.removeFromLeft(120).reduced(4));
    refreshBackendButton_.setBounds(projectBar.removeFromLeft(150).reduced(4));
    rebuildPreviewButton_.setBounds(projectBar.removeFromLeft(150).reduced(4));
    settingsButton_.setBounds(projectBar.removeFromLeft(100).reduced(4));
    exportMixButton_.setBounds(projectBar.removeFromLeft(120).reduced(4));
    exportRegionButton_.setBounds(projectBar.removeFromLeft(140).reduced(4));
    exportStemsButton_.setBounds(projectBar.removeFromLeft(130).reduced(4));
    projectStatusLabel_.setBounds(projectBar.reduced(6, 4));
    transportBar_.setBounds(transport.reduced(4));
    trackListView_.setBounds(tracks.reduced(4));
    timelineView_.setBounds(area.reduced(4));
    inspectorPanel_.setBounds(inspector.reduced(4));
    taskPanel_.setBounds(taskArea.reduced(4));
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

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".wav"))
        {
            const auto logCount = controller_.logger().lineCount();
            if (!controller_.importAudio(file.toStdString()))
            {
                showOperationFailure("Import WAV", "The dropped WAV file could not be imported.", logCount);
            }
        }
    }

    timelineView_.repaint();
    trackListView_.repaint();
}

void MainComponent::createNewProject()
{
    juce::FileChooser chooser("Choose project folder",
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*");
    if (!chooser.browseForDirectory())
    {
        return;
    }

    const auto root = chooser.getResult();
    const auto logCount = controller_.logger().lineCount();
    if (!controller_.createProject(root.getFileNameWithoutExtension().toStdString(), root.getFullPathName().toStdString()))
    {
        showOperationFailure("New Project", "The project folder could not be initialized.", logCount);
        return;
    }
    timelineView_.repaint();
    trackListView_.repaint();
    inspectorPanel_.repaint();
    taskPanel_.repaint();
}

void MainComponent::openExistingProject()
{
    juce::FileChooser chooser("Open project JSON",
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.json");
    if (!chooser.browseForFileToOpen())
    {
        return;
    }

    const auto logCount = controller_.logger().lineCount();
    if (!controller_.openProject(chooser.getResult().getFullPathName().toStdString()))
    {
        showOperationFailure("Open Project", "The selected project file could not be opened.", logCount);
        return;
    }
    timelineView_.repaint();
    trackListView_.repaint();
    inspectorPanel_.repaint();
    taskPanel_.repaint();
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
    juce::FileChooser chooser("Import WAV file",
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.wav");
    if (!chooser.browseForFileToOpen())
    {
        return;
    }

    const auto logCount = controller_.logger().lineCount();
    if (!controller_.importAudio(chooser.getResult().getFullPathName().toStdString()))
    {
        showOperationFailure("Import WAV", "The selected WAV file could not be imported.", logCount);
        return;
    }
    timelineView_.repaint();
    trackListView_.repaint();
    inspectorPanel_.repaint();
    taskPanel_.repaint();
}

void MainComponent::exportFullMixFile()
{
    juce::FileChooser chooser("Export mix WAV",
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("mix_export.wav"),
                              "*.wav");
    if (!chooser.browseForFileToSave(true))
    {
        return;
    }

    const auto logCount = controller_.logger().lineCount();
    if (!controller_.exportFullMix(chooser.getResult().getFullPathName().toStdString()))
    {
        showOperationFailure("Export Mix", "Mix export failed.", logCount);
        return;
    }
    showOperationInfo("Export Mix", "Mix WAV exported successfully.");
    taskPanel_.repaint();
}

void MainComponent::exportSelectedRegionFile()
{
    juce::FileChooser chooser("Export selected region WAV",
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("region_export.wav"),
                              "*.wav");
    if (!chooser.browseForFileToSave(true))
    {
        return;
    }

    const auto logCount = controller_.logger().lineCount();
    if (!controller_.exportSelectedRegion(chooser.getResult().getFullPathName().toStdString()))
    {
        showOperationFailure("Export Region", "Select a timeline region before exporting.", logCount);
        return;
    }
    showOperationInfo("Export Region", "Selected region WAV exported successfully.");
    taskPanel_.repaint();
}

void MainComponent::exportStemTracksFiles()
{
    juce::FileChooser chooser("Choose stem export folder",
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*");
    if (!chooser.browseForDirectory())
    {
        return;
    }

    const auto logCount = controller_.logger().lineCount();
    if (!controller_.exportStemTracks(chooser.getResult().getFullPathName().toStdString()))
    {
        showOperationFailure("Export Stems", "No stem tracks are available to export.", logCount);
        return;
    }
    showOperationInfo("Export Stems", "Stem WAV files exported successfully.");
    taskPanel_.repaint();
}

void MainComponent::refreshBackendConnection()
{
    const auto logCount = controller_.logger().lineCount();
    if (controller_.refreshBackendStatus())
    {
        resized();
        transportBar_.refresh();
        showOperationInfo("Refresh Backend", "Backend connection refreshed successfully.");
        return;
    }

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
    auto settings = controller_.currentSettings();

    juce::AlertWindow window("App Settings", "Configure local app settings", juce::AlertWindow::NoIcon);
    window.addTextEditor("backend_url", settings.backendUrl, "Backend URL");
    window.addTextEditor("cache_directory", settings.cacheDirectory.string(), "Cache Directory");
    window.addTextEditor("default_sample_rate", std::to_string(settings.defaultSampleRate), "Default Sample Rate");
    window.addButton("Save", 1);
    window.addButton("Cancel", 0);

    if (window.runModalLoop() != 1)
    {
        return;
    }

    settings.backendUrl = window.getTextEditor("backend_url")->getText().toStdString();
    settings.cacheDirectory = window.getTextEditor("cache_directory")->getText().toStdString();
    settings.defaultSampleRate = window.getTextEditor("default_sample_rate")->getText().getIntValue();
    const auto logCount = controller_.logger().lineCount();
    if (!controller_.saveSettings(settings))
    {
        showOperationFailure("App Settings", "Settings could not be saved.", logCount);
        return;
    }
    showOperationInfo("App Settings", "Settings saved. Restart the app to fully apply backend URL changes.");
    taskPanel_.repaint();
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
    rebuildPreviewButton_.setEnabled(hasClips && !controller_.transport().canUseProjectPlayback());
}

void MainComponent::refreshProjectStatusLabel()
{
    const auto& state = controller_.projectManager().state();
    juce::String text = "Project: ";
    text += state.projectName.empty() ? "Untitled" : juce::String(state.projectName);
    text += " | ";
    text += juce::String(state.sampleRate);
    text += " Hz";
    text += " | Tracks: ";
    text += juce::String(static_cast<int>(state.tracks.size()));
    text += " | Clips: ";
    text += juce::String(static_cast<int>(state.clips.size()));
    text += " | Timeline: ";
    text += juce::String(state.engineState.timelineBackend);
    text += " | Transport: ";
    text += juce::String(state.engineState.transportBackend);
    text += " | Sync: ";
    text += juce::String(state.engineState.tracktionSyncState);
    if (!state.engineState.tracktionSyncReason.empty())
    {
        text += " | Reason: ";
        text += juce::String(state.engineState.tracktionSyncReason);
    }

    if (const auto projectFile = controller_.projectFilePath(); projectFile.has_value())
    {
        text += " | ";
        text += juce::String(*projectFile);
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

void MainComponent::timerCallback()
{
    if (startupNoticeLabel_.isVisible() != !controller_.startupNotice().empty())
    {
        resized();
    }
    controller_.transport().tick(0.25);
    controller_.maintainPreviewPlayback();
    controller_.syncTransportToSelection();
    controller_.pollTasks();
    ++autosaveTickCounter_;
    if (autosaveTickCounter_ >= 40)
    {
        autosaveTickCounter_ = 0;
        controller_.autosaveIfNeeded();
    }
    refreshActionAvailability();
    refreshProjectStatusLabel();
    transportBar_.refresh();
    timelineView_.repaint();
    trackListView_.repaint();
    inspectorPanel_.repaint();
    taskPanel_.repaint();
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
        controller_.deleteSelectedClip();
        timelineView_.repaint();
        inspectorPanel_.repaint();
        taskPanel_.repaint();
        return true;
    }

    return false;
}
}
#endif
