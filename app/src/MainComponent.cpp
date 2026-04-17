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
          [&controller](double timelineSec)
          {
              controller.seekTimelinePlayhead(timelineSec);
          },
          [&controller]()
          {
              return controller.projectPlaybackDurationSec();
          },
          [&controller]()
          {
              return controller.projectTempo();
          },
          [&controller]() { return controller.backendStatusSummary(); })
    , trackListView_(
          controller.timeline(),
          controller.projectManager(),
          [&controller]()
          {
              controller.notifyProjectMixChanged();
          })
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

    addAndMakeVisible(menuButton_);
    addAndMakeVisible(startupNoticeLabel_);
    addAndMakeVisible(dismissStartupNoticeButton_);
    addAndMakeVisible(transportBar_);
    addAndMakeVisible(trackListView_);
    timelineViewport_.setViewedComponent(&timelineView_, false);
    timelineViewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(timelineViewport_);
    inspectorViewport_.setViewedComponent(&inspectorPanel_, false);
    inspectorViewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(inspectorViewport_);
    taskViewport_.setViewedComponent(&taskPanel_, false);
    taskViewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(taskViewport_);

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
    styleToolbarButton(zoomOutButton_);
    styleToolbarButton(zoomResetButton_);
    styleToolbarButton(zoomInButton_);
    styleToolbarButton(settingsButton_);
    styleToolbarButton(exportMixButton_);
    styleToolbarButton(exportRegionButton_);
    styleToolbarButton(exportStemsButton_);
    styleToolbarButton(dismissStartupNoticeButton_);
    menuButton_.setButtonText("≡ Menu");

    menuButton_.onClick = [this] { showMainMenu(); };
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
    lastTransportTickMs_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(13, 15, 19));
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
    auto taskArea = area.removeFromBottom(78);
    auto inspector = area.removeFromRight(378);
    auto tracks = area.removeFromLeft(198);

    menuButton_.setBounds(transport.removeFromLeft(86).reduced(4));
    transportBar_.setBounds(transport.reduced(4));
    trackListView_.setBounds(tracks.reduced(4));
    timelineViewport_.setBounds(area.reduced(4));
    timelineView_.updateContentSize();
    inspectorViewport_.setBounds(inspector.reduced(4));
    inspectorPanel_.setSize(juce::jmax(340, inspectorViewport_.getWidth() - 14), 1240);
    taskViewport_.setBounds(taskArea.reduced(4));
    taskPanel_.updateContentSize(juce::jmax(320, taskViewport_.getWidth() - 14));
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

    timelineView_.updateContentSize();
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

    timelineView_.repaint();
    timelineView_.updateContentSize();
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

            timelineView_.updateContentSize();
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

            timelineView_.updateContentSize();
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

void MainComponent::timerCallback()
{
    if (startupNoticeLabel_.isVisible() != !controller_.startupNotice().empty())
    {
        resized();
    }

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto deltaSec = juce::jlimit(1.0 / 240.0, 0.05, (nowMs - lastTransportTickMs_) / 1000.0);
    lastTransportTickMs_ = nowMs;

    controller_.transport().tick(deltaSec);
    controller_.refreshPlaybackUiState();
    controller_.maintainPreviewPlayback();
    ++taskPollTickCounter_;
    ++autosaveTickCounter_;

    const bool shouldPollTasks = taskPollTickCounter_ >= 16;
    if (shouldPollTasks)
    {
        taskPollTickCounter_ = 0;
        controller_.pollTasks();
    }

    if (autosaveTickCounter_ >= 600)
    {
        autosaveTickCounter_ = 0;
        controller_.autosaveIfNeeded();
    }

    transportBar_.refresh();
    taskPanel_.updateContentSize(juce::jmax(320, taskViewport_.getWidth() - 14));
    timelineView_.repaint();

    if (shouldPollTasks)
    {
        refreshActionAvailability();
        refreshProjectStatusLabel();
        trackListView_.repaint();
        inspectorPanel_.repaint();
        taskPanel_.repaint();
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





