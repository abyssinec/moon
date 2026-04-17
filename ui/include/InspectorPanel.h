#pragma once

#include <functional>

#include "Logger.h"
#include "ProjectManager.h"
#include "TaskManager.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class InspectorPanel final : public juce::Component
{
public:
    InspectorPanel(moon::engine::ProjectManager& projectManager,
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
                   std::function<void()> onStems);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    moon::engine::ProjectManager& projectManager_;
    moon::engine::TaskManager& taskManager_;
    moon::engine::Logger& logger_;
    std::function<std::string()> promptProvider_;
    std::function<void(double)> onGainChanged_;
    std::function<void(double)> onFadeInChanged_;
    std::function<void(double)> onFadeOutChanged_;
    std::function<void(double)> onTrimLeft_;
    std::function<void(double)> onTrimRight_;
    std::function<void()> onClearRegion_;
    std::function<void()> onSplitClip_;
    std::function<void()> onActivateTake_;
    std::function<void()> onDuplicateClip_;
    std::function<void()> onDeleteClip_;
    std::function<void(double)> onCrossfadePrevious_;
    std::function<void(double)> onCrossfadeNext_;
    std::function<void(const std::string&)> onRewrite_;
    std::function<void(const std::string&)> onAddLayer_;
    std::function<void()> onStems_;
    bool refreshingControls_{false};
    juce::Label title_;
    juce::Label selectionSummary_;
    juce::Label clipDetailsLabel_;
    juce::Label gainLabel_;
    juce::Label fadeLabel_;
    juce::Label crossfadeLabel_;
    juce::TextEditor promptEditor_;
    juce::Slider strengthSlider_;
    juce::Slider gainSlider_;
    juce::Slider fadeInSlider_;
    juce::Slider fadeOutSlider_;
    juce::Slider crossfadeSlider_;
    juce::ToggleButton preserveTiming_{"Preserve timing"};
    juce::ToggleButton preserveMelody_{"Preserve melody"};
    juce::TextButton trimLeftButton_{"Trim Left -0.10s"};
    juce::TextButton trimRightButton_{"Trim Right -0.10s"};
    juce::TextButton extendLeftButton_{"Extend Left +0.10s"};
    juce::TextButton extendRightButton_{"Extend Right +0.10s"};
    juce::TextButton clearRegionButton_{"Clear Region"};
    juce::TextButton splitClipButton_{"Split At Playhead"};
    juce::TextButton activateTakeButton_{"Activate Selected Take"};
    juce::TextButton duplicateClipButton_{"Duplicate clip"};
    juce::TextButton deleteClipButton_{"Delete clip"};
    juce::TextButton crossfadePreviousButton_{"Apply Prev Crossfade"};
    juce::TextButton crossfadeNextButton_{"Apply Next Crossfade"};
    juce::TextButton stemsButton_{"Separate into stems"};
    juce::TextButton rewriteButton_{"Rewrite selected region"};
    juce::TextButton addLayerButton_{"Add generated layer"};
};
}
#else
namespace moon::ui
{
class InspectorPanel
{
public:
    InspectorPanel(moon::engine::ProjectManager&, moon::engine::TaskManager&, moon::engine::Logger&) {}
};
}
#endif
