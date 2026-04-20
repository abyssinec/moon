#pragma once

#include <functional>
#include <string>
#include <vector>

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
    void setAvailableModels(const std::vector<std::string>& models);
    void refreshTaskState(const moon::engine::TaskManager& taskManager);

    int preferredHeight() const noexcept;
    bool isExpanded() const noexcept { return expanded_; }

    std::function<void()> onHeightChanged;

private:
    void toggleExpanded();
    void showCategoryMenu();
    void showModelMenu();
    void submitCreate();
    void refreshControls();
    moon::engine::MusicGenerationRequest buildRequest() const;
    juce::String categoryLabel() const;
    juce::String modelLabel() const;
    bool requestLooksEmpty() const;

    std::function<MusicGenerationSubmission(const moon::engine::MusicGenerationRequest&)> createCallback_;
    std::vector<std::string> availableModels_;
    std::string selectedModel_;
    moon::engine::MusicGenerationCategory selectedCategory_{moon::engine::MusicGenerationCategory::Song};
    bool expanded_{false};
    bool generating_{false};
    std::string activeJobId_;
    juce::String statusText_{"Acestep generation ready"};

    juce::TextButton expandButton_{"+"};
    juce::TextButton categoryButton_{"Song"};
    juce::TextButton modelButton_{"Model"};
    juce::TextButton createButton_{"Create"};
    juce::Label statusLabel_;
    juce::Label stylesLabel_;
    juce::Label lyricsLabel_;
    juce::TextEditor stylesEditor_;
    juce::TextEditor lyricsEditor_;
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
