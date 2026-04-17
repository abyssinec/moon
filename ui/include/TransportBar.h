#pragma once

#include <functional>

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
                 std::function<std::string()> statusProvider);
    void resized() override;
    void refresh();

private:
    moon::engine::TransportFacade& transport_;
    moon::engine::Logger& logger_;
    std::function<void()> playCallback_;
    std::function<void()> pauseCallback_;
    std::function<void()> stopCallback_;
    std::function<std::string()> statusProvider_;
    juce::TextButton playButton_{"Play"};
    juce::TextButton pauseButton_{"Pause"};
    juce::TextButton stopButton_{"Stop"};
    juce::ToggleButton loopToggle_{"Loop"};
    juce::Label timeLabel_;
    juce::Label statusLabel_;
};
}
#else
namespace moon::ui
{
class TransportBar
{
public:
    TransportBar(moon::engine::TransportFacade&, moon::engine::Logger&, std::function<void()>, std::function<void()>, std::function<void()>, std::function<std::string()>) {}
};
}
#endif
