#pragma once

#include <functional>
#include <utility>

#include "Logger.h"
#include "TransportFacade.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class TransportBar final : public juce::Component
{
public:
    TransportBar(moon::engine::TransportFacade& transport,
                 moon::engine::Logger& logger,
                 std::function<void()> playCallback,
                 std::function<void()> pauseCallback,
                 std::function<void()> stopCallback,
                 std::function<void(double)> seekCallback,
                 std::function<double()> durationProvider,
                 std::function<std::pair<int, int>()> timeSignatureProvider,
                 std::function<double()> tempoProvider,
                 std::function<void(int, int)> timeSignatureChangeCallback,
                 std::function<void(double)> tempoChangeCallback,
                 std::function<std::string()> statusProvider);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();

private:
    void showTimeSignatureMenu();
    void showTempoEditor();

    moon::engine::TransportFacade& transport_;
    moon::engine::Logger& logger_;
    std::function<void()> playCallback_;
    std::function<void()> pauseCallback_;
    std::function<void()> stopCallback_;
    std::function<void(double)> seekCallback_;
    std::function<double()> durationProvider_;
    std::function<std::pair<int, int>()> timeSignatureProvider_;
    std::function<double()> tempoProvider_;
    std::function<void(int, int)> timeSignatureChangeCallback_;
    std::function<void(double)> tempoChangeCallback_;
    std::function<std::string()> statusProvider_;
    bool refreshingPosition_{false};
    juce::TextButton playButton_{"Play"};
    juce::TextButton pauseButton_{"Pause"};
    juce::TextButton stopButton_{"Stop"};
    juce::ToggleButton loopToggle_{"Loop"};
    juce::Slider positionSlider_;
    juce::TextButton timeSignatureButton_{"4/4"};
    juce::TextButton bpmButton_{"120 BPM"};
    juce::Label timeLabel_;
    juce::Label statusLabel_;
    juce::Component::SafePointer<juce::CallOutBox> tempoCallout_;
};
}
#else
namespace moon::ui
{
class TransportBar
{
public:
    TransportBar(moon::engine::TransportFacade&, moon::engine::Logger&, std::function<void()>, std::function<void()>, std::function<void()>, std::function<void(double)>, std::function<double()>, std::function<std::pair<int, int>()>, std::function<double()>, std::function<void(int, int)>, std::function<void(double)>, std::function<std::string()>) {}
};
}
#endif
