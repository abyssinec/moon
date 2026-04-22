#pragma once

#include <functional>
#include <map>
#include <string>

#include "ModelManager.h"
#include "MusicGeneration.h"
#include "TaskManager.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
struct MusicGenerationSubmission
{
    bool accepted{false};
    std::string jobId;
    std::string errorMessage;
};

class AIGenerationPanel final : public juce::Component
{
public:
    AIGenerationPanel();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setCreateCallback(std::function<MusicGenerationSubmission(const moon::engine::MusicGenerationRequest&)> callback);
    void setCancelCallback(std::function<bool(const std::string&)> callback);
    void setModelRegistrySnapshot(const moon::engine::ModelRegistrySnapshot& snapshot);
    void setModelSelectionCallback(std::function<bool(moon::engine::ModelCapability, const std::string&, std::string&)> callback);
    void setOpenModelManagerCallback(std::function<void(moon::engine::ModelCapability)> callback);
    void refreshTaskState(const moon::engine::TaskManager& taskManager);

    int preferredHeight() const noexcept;
    bool isExpanded() const noexcept { return expanded_; }

    std::function<void()> onHeightChanged;

private:
    void toggleExpanded();
    void showTargetMenu();
    void showDeviceMenu();
    void showModelMenu();
    void submitCreate();
    void refreshControls();
    void updateSecondaryProfile();
    void storeSecondaryPromptForCurrentTarget();
    void restoreSecondaryPromptForCurrentTarget();
    moon::engine::MusicGenerationRequest buildRequest() const;
    bool requestLooksEmpty() const;
    juce::String currentTargetLabel() const;
    juce::String currentDeviceLabel() const;
    juce::String activeModelLabel() const;
    juce::String activeModelStatusLabel() const;
    std::string activeModelId() const;
    bool activeModelReady() const;
    std::string activeModelVersion() const;
    std::string activeModelPath() const;
    moon::engine::ModelCapability currentCapability() const noexcept;
    moon::engine::GenerationTargetProfile currentTargetProfile() const noexcept;

    std::function<MusicGenerationSubmission(const moon::engine::MusicGenerationRequest&)> createCallback_;
    std::function<bool(const std::string&)> cancelCallback_;
    std::function<bool(moon::engine::ModelCapability, const std::string&, std::string&)> modelSelectionCallback_;
    std::function<void(moon::engine::ModelCapability)> openModelManagerCallback_;
    moon::engine::ModelRegistrySnapshot modelRegistrySnapshot_;
    std::map<moon::engine::GenerationTarget, std::string> secondaryPromptByTarget_;
    moon::engine::GenerationTarget selectedTarget_{moon::engine::GenerationTarget::Song};
    moon::engine::ComputeDevicePreference selectedDevicePreference_{moon::engine::ComputeDevicePreference::Auto};
    bool expanded_{false};
    bool generating_{false};
    std::string activeJobId_;
    double generationProgress_{0.0};
    juce::String statusText_{"Generation ready"};

    juce::TextButton expandButton_{">"};
    juce::TextButton modelButton_{"No model"};
    juce::TextButton manageModelsButton_{"Models..."};
    juce::TextButton targetButton_{"Song"};
    juce::TextButton deviceButton_{"Auto"};
    juce::TextButton createButton_{"Create"};
    juce::TextButton cancelButton_{"Cancel"};
    juce::Label statusLabel_;
    juce::ProgressBar progressBar_{generationProgress_};
    juce::Label modelCaptionLabel_;
    juce::Label stylesLabel_;
    juce::Label secondaryLabel_;
    juce::TextEditor stylesEditor_;
    juce::TextEditor secondaryEditor_;
};
}
#else
namespace moon::ui
{
struct MusicGenerationSubmission
{
    bool accepted{false};
    std::string jobId;
    std::string errorMessage;
};

class AIGenerationPanel
{
public:
    AIGenerationPanel() = default;
};
}
#endif
