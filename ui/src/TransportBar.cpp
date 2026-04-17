#include "TransportBar.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
TransportBar::TransportBar(moon::engine::TransportFacade& transport,
                           moon::engine::Logger& logger,
                           std::function<void()> playCallback,
                           std::function<void()> pauseCallback,
                           std::function<void()> stopCallback,
                           std::function<std::string()> statusProvider)
    : transport_(transport)
    , logger_(logger)
    , playCallback_(std::move(playCallback))
    , pauseCallback_(std::move(pauseCallback))
    , stopCallback_(std::move(stopCallback))
    , statusProvider_(std::move(statusProvider))
{
    addAndMakeVisible(playButton_);
    addAndMakeVisible(pauseButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(loopToggle_);
    addAndMakeVisible(timeLabel_);
    addAndMakeVisible(statusLabel_);

    timeLabel_.setText("00:00.000", juce::dontSendNotification);
    statusLabel_.setText("Backend: unknown", juce::dontSendNotification);
    playButton_.onClick = [this]
    {
        if (playCallback_)
        {
            playCallback_();
            return;
        }
        transport_.play();
    };
    pauseButton_.onClick = [this]
    {
        if (pauseCallback_)
        {
            pauseCallback_();
            return;
        }
        transport_.pause();
    };
    stopButton_.onClick = [this]
    {
        if (stopCallback_)
        {
            stopCallback_();
            return;
        }
        transport_.stop();
    };
}

void TransportBar::resized()
{
    auto area = getLocalBounds();
    playButton_.setBounds(area.removeFromLeft(80).reduced(4));
    pauseButton_.setBounds(area.removeFromLeft(80).reduced(4));
    stopButton_.setBounds(area.removeFromLeft(80).reduced(4));
    loopToggle_.setBounds(area.removeFromLeft(100).reduced(4));
    timeLabel_.setBounds(area.removeFromLeft(120).reduced(4));
    statusLabel_.setBounds(area.reduced(4));
}

void TransportBar::refresh()
{
    const auto seconds = transport_.playheadSec();
    const auto minutesPart = static_cast<int>(seconds) / 60;
    const auto secondsPart = static_cast<int>(seconds) % 60;
    const auto millisPart = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000.0);
    const auto text = juce::String::formatted("%02d:%02d.%03d", minutesPart, secondsPart, millisPart);
    timeLabel_.setText(text, juce::dontSendNotification);
    if (statusProvider_)
    {
        statusLabel_.setText(statusProvider_(), juce::dontSendNotification);
    }
}
}
#endif
