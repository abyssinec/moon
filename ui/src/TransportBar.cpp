#include "TransportBar.h"

#include <algorithm>
#include <cmath>

#if MOON_HAS_JUCE
namespace moon::ui
{
namespace
{
class TempoEditorContent final : public juce::Component
{
public:
    TempoEditorContent(double currentTempo, std::function<void(double)> applyCallback)
        : applyCallback_(std::move(applyCallback))
    {
        addAndMakeVisible(minusButton_);
        addAndMakeVisible(valueButton_);
        addAndMakeVisible(plusButton_);
        addAndMakeVisible(editor_);

        minusButton_.setButtonText("-");
        plusButton_.setButtonText("+");
        valueButton_.setButtonText(juce::String(static_cast<int>(std::round(currentTempo))) + " BPM");
        for (auto* button : {&minusButton_, &valueButton_, &plusButton_})
        {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(33, 36, 42));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(43, 169, 237));
            button->setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
        }

        editor_.setVisible(false);
        editor_.setText(juce::String(static_cast<int>(std::round(currentTempo))), juce::dontSendNotification);
        editor_.setInputRestrictions(3, "0123456789");
        editor_.onReturnKey = [this] { commitEditorValue(); };
        editor_.onEscapeKey = [this] { hideEditor(); };
        editor_.onFocusLost = [this] { commitEditorValue(); };

        minusButton_.onClick = [this, currentTempo]
        {
            if (applyCallback_)
            {
                applyCallback_(std::clamp(currentTempo - 1.0, 20.0, 300.0));
            }
        };
        plusButton_.onClick = [this, currentTempo]
        {
            if (applyCallback_)
            {
                applyCallback_(std::clamp(currentTempo + 1.0, 20.0, 300.0));
            }
        };
        valueButton_.onClick = [this]
        {
            valueButton_.setVisible(false);
            editor_.setVisible(true);
            editor_.grabKeyboardFocus();
            editor_.selectAll();
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        minusButton_.setBounds(area.removeFromLeft(32).reduced(2));
        plusButton_.setBounds(area.removeFromRight(32).reduced(2));
        valueButton_.setBounds(area.reduced(2));
        editor_.setBounds(valueButton_.getBounds());
    }

private:
    void hideEditor()
    {
        editor_.setVisible(false);
        valueButton_.setVisible(true);
    }

    void commitEditorValue()
    {
        const auto parsed = editor_.getText().getDoubleValue();
        if (applyCallback_ && parsed > 0.0)
        {
            applyCallback_(std::clamp(parsed, 20.0, 300.0));
        }
        hideEditor();
    }

    std::function<void(double)> applyCallback_;
    juce::TextButton minusButton_;
    juce::TextButton valueButton_;
    juce::TextButton plusButton_;
    juce::TextEditor editor_;
};
}

TransportBar::TransportBar(moon::engine::TransportFacade& transport,
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
                           std::function<std::string()> statusProvider)
    : transport_(transport)
    , logger_(logger)
    , playCallback_(std::move(playCallback))
    , pauseCallback_(std::move(pauseCallback))
    , stopCallback_(std::move(stopCallback))
    , seekCallback_(std::move(seekCallback))
    , durationProvider_(std::move(durationProvider))
    , timeSignatureProvider_(std::move(timeSignatureProvider))
    , tempoProvider_(std::move(tempoProvider))
    , timeSignatureChangeCallback_(std::move(timeSignatureChangeCallback))
    , tempoChangeCallback_(std::move(tempoChangeCallback))
    , statusProvider_(std::move(statusProvider))
{
    addAndMakeVisible(playButton_);
    addAndMakeVisible(pauseButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(loopToggle_);
    addAndMakeVisible(positionSlider_);
    addAndMakeVisible(timeSignatureButton_);
    addAndMakeVisible(bpmButton_);
    addAndMakeVisible(timeLabel_);
    addAndMakeVisible(statusLabel_);

    positionSlider_.setRange(0.0, 1.0, 0.001);
    positionSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    positionSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    positionSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGB(48, 53, 60));
    positionSlider_.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(43, 169, 237));
    positionSlider_.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(83, 203, 255));
    timeLabel_.setText("00:00.000", juce::dontSendNotification);
    statusLabel_.setText("Backend: unknown", juce::dontSendNotification);
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
    styleButton(timeSignatureButton_);
    styleButton(bpmButton_);
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
    timeSignatureButton_.onClick = [this] { showTimeSignatureMenu(); };
    bpmButton_.onClick = [this] { showTempoEditor(); };
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
    timeSignatureButton_.setBounds(area.removeFromLeft(72).reduced(4));
    bpmButton_.setBounds(area.removeFromLeft(86).reduced(4));
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
    const auto timeSignature = timeSignatureProvider_ ? timeSignatureProvider_() : std::pair<int, int>{4, 4};
    const auto tempo = tempoProvider_ ? tempoProvider_() : 120.0;
    timeSignatureButton_.setButtonText(juce::String(timeSignature.first) + "/" + juce::String(timeSignature.second));
    bpmButton_.setButtonText(juce::String(static_cast<int>(std::round(tempo))) + " BPM");
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

void TransportBar::showTimeSignatureMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "2/4");
    menu.addItem(2, "3/4");
    menu.addItem(3, "4/4");
    menu.addItem(4, "5/4");
    menu.addItem(5, "6/8");
    menu.addItem(6, "7/8");
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&timeSignatureButton_),
        [this](int result)
        {
            if (!timeSignatureChangeCallback_)
            {
                return;
            }

            switch (result)
            {
            case 1: timeSignatureChangeCallback_(2, 4); break;
            case 2: timeSignatureChangeCallback_(3, 4); break;
            case 3: timeSignatureChangeCallback_(4, 4); break;
            case 4: timeSignatureChangeCallback_(5, 4); break;
            case 5: timeSignatureChangeCallback_(6, 8); break;
            case 6: timeSignatureChangeCallback_(7, 8); break;
            default: break;
            }
            refresh();
        });
}

void TransportBar::showTempoEditor()
{
    auto content = std::make_unique<TempoEditorContent>(
        tempoProvider_ ? tempoProvider_() : 120.0,
        [this](double newTempo)
        {
            if (tempoChangeCallback_)
            {
                tempoChangeCallback_(newTempo);
            }
            refresh();
            if (tempoCallout_ != nullptr)
            {
                tempoCallout_->dismiss();
            }
        });
    content->setSize(162, 42);
    tempoCallout_ = &juce::CallOutBox::launchAsynchronously(std::move(content), bpmButton_.getScreenBounds(), nullptr);
}
}
#endif
