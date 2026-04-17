#include "TransportBar.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
TransportBar::TransportBar(moon::engine::TransportFacade& transport,
                           moon::engine::Logger& logger,
                           std::function<void()> playCallback,
                           std::function<void()> pauseCallback,
                           std::function<void()> stopCallback,
                           std::function<void(double)> seekCallback,
                           std::function<double()> durationProvider,
                           std::function<double()> tempoProvider,
                           std::function<std::string()> statusProvider)
    : transport_(transport)
    , logger_(logger)
    , playCallback_(std::move(playCallback))
    , pauseCallback_(std::move(pauseCallback))
    , stopCallback_(std::move(stopCallback))
    , seekCallback_(std::move(seekCallback))
    , durationProvider_(std::move(durationProvider))
    , tempoProvider_(std::move(tempoProvider))
    , statusProvider_(std::move(statusProvider))
{
    addAndMakeVisible(playButton_);
    addAndMakeVisible(pauseButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(loopToggle_);
    addAndMakeVisible(positionSlider_);
    addAndMakeVisible(meterLabel_);
    addAndMakeVisible(timeLabel_);
    addAndMakeVisible(statusLabel_);

    positionSlider_.setRange(0.0, 1.0, 0.001);
    positionSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    positionSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    positionSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGB(48, 53, 60));
    positionSlider_.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(43, 169, 237));
    positionSlider_.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(83, 203, 255));
    meterLabel_.setText("4/4 | 120 BPM", juce::dontSendNotification);
    timeLabel_.setText("00:00.000", juce::dontSendNotification);
    statusLabel_.setText("Backend: unknown", juce::dontSendNotification);
    meterLabel_.setJustificationType(juce::Justification::centredLeft);
    meterLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(173, 179, 189));
    timeLabel_.setJustificationType(juce::Justification::centredLeft);
    timeLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 232, 235));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(133, 140, 150));

    auto styleButton = [](juce::Button& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(33, 36, 42));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(43, 169, 237));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    };
    styleButton(playButton_);
    styleButton(pauseButton_);
    styleButton(stopButton_);
    loopToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.82f));
    playButton_.setButtonText("Play");
    pauseButton_.setButtonText("Pause");
    stopButton_.setButtonText("Stop");

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
    positionSlider_.onValueChange = [this]
    {
        if (refreshingPosition_ || !positionSlider_.isMouseButtonDown())
        {
            return;
        }

        if (seekCallback_)
        {
            seekCallback_(positionSlider_.getValue());
        }
        else
        {
            transport_.seek(positionSlider_.getValue());
        }
    };
}

void TransportBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGB(18, 20, 24));
    g.fillRoundedRectangle(bounds.reduced(0.0f, 2.0f), 10.0f);
    g.setColour(juce::Colour::fromRGB(44, 48, 54));
    g.drawRoundedRectangle(bounds.reduced(0.5f, 2.5f), 10.0f, 1.0f);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced(10, 8);
    playButton_.setBounds(area.removeFromLeft(70).reduced(4));
    pauseButton_.setBounds(area.removeFromLeft(74).reduced(4));
    stopButton_.setBounds(area.removeFromLeft(70).reduced(4));
    loopToggle_.setBounds(area.removeFromLeft(76).reduced(4));
    meterLabel_.setBounds(area.removeFromLeft(110).reduced(4));
    timeLabel_.setBounds(area.removeFromLeft(176).reduced(4));
    positionSlider_.setBounds(area.removeFromLeft(420).reduced(4));
    statusLabel_.setBounds(area.reduced(3));
}

void TransportBar::refresh()
{
    const auto seconds = transport_.playheadSec();
    const auto durationSec = durationProvider_ ? durationProvider_() : transport_.sourceDurationSec();
    const auto minutesPart = static_cast<int>(seconds) / 60;
    const auto secondsPart = static_cast<int>(seconds) % 60;
    const auto millisPart = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000.0);
    const auto totalMinutes = static_cast<int>(durationSec) / 60;
    const auto totalSeconds = static_cast<int>(durationSec) % 60;
    const auto totalMillis = static_cast<int>((durationSec - static_cast<int>(durationSec)) * 1000.0);
    const auto text = juce::String::formatted(
        "%02d:%02d.%03d / %02d:%02d.%03d",
        minutesPart,
        secondsPart,
        millisPart,
        totalMinutes,
        totalSeconds,
        totalMillis);
    const auto tempo = tempoProvider_ ? tempoProvider_() : 120.0;
    meterLabel_.setText("4/4 | " + juce::String(tempo, 0) + " BPM", juce::dontSendNotification);
    timeLabel_.setText(text, juce::dontSendNotification);
    refreshingPosition_ = true;
    positionSlider_.setRange(0.0, juce::jmax(0.1, durationSec), 0.001);
    positionSlider_.setValue(juce::jlimit(0.0, juce::jmax(0.1, durationSec), seconds), juce::dontSendNotification);
    refreshingPosition_ = false;
    if (statusProvider_)
    {
        statusLabel_.setText(statusProvider_(), juce::dontSendNotification);
    }
}
}
#endif
