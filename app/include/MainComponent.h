#pragma once

#include "AppController.h"

#if MOON_HAS_JUCE
#include <memory>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "InspectorPanel.h"
#include "TaskPanel.h"
#include "TimelineView.h"
#include "TrackListView.h"
#include "TransportBar.h"

namespace moon::app
{
class MainComponent final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit MainComponent(AppController& controller);
    ~MainComponent() override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void createNewProject();
    void openExistingProject();
    void saveCurrentProject();
    void importAudioFile();
    void exportFullMixFile();
    void exportSelectedRegionFile();
    void exportStemTracksFiles();
    void refreshBackendConnection();
    void rebuildPreviewCache();
    void editSettings();
    void refreshActionAvailability();
    void refreshProjectStatusLabel();
    void showOperationFailure(const juce::String& title, const juce::String& fallbackMessage, std::size_t previousLogCount);
    void showOperationInfo(const juce::String& title, const juce::String& message);
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

    AppController& controller_;
    juce::AudioDeviceManager audioDeviceManager_;
    std::unique_ptr<juce::AudioSourcePlayer> audioSourcePlayer_;
    juce::TextButton newProjectButton_{"New"};
    juce::TextButton openProjectButton_{"Open"};
    juce::TextButton saveProjectButton_{"Save"};
    juce::TextButton importAudioButton_{"Import WAV"};
    juce::TextButton refreshBackendButton_{"Refresh Backend"};
    juce::TextButton rebuildPreviewButton_{"Rebuild Preview"};
    juce::TextButton settingsButton_{"Settings"};
    juce::Label projectStatusLabel_;
    juce::TextButton exportMixButton_{"Export Mix"};
    juce::TextButton exportRegionButton_{"Export Region"};
    juce::TextButton exportStemsButton_{"Export Stems"};
    juce::Label startupNoticeLabel_;
    juce::TextButton dismissStartupNoticeButton_{"Dismiss"};
    int autosaveTickCounter_{0};
    moon::ui::TransportBar transportBar_;
    moon::ui::TrackListView trackListView_;
    moon::ui::TimelineView timelineView_;
    moon::ui::InspectorPanel inspectorPanel_;
    moon::ui::TaskPanel taskPanel_;
};
}
#else
namespace moon::app
{
class MainComponent
{
public:
    explicit MainComponent(AppController&) {}
};
}
#endif
