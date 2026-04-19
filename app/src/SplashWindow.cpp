#include "SplashWindow.h"

#include "AppConfig.h"

namespace moon::app
{
SplashContent::SplashContent()
{
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(headlineLabel_);
    addAndMakeVisible(detailLabel_);
    addAndMakeVisible(retryButton_);
    addAndMakeVisible(continueFallbackButton_);
    addAndMakeVisible(showDetailsButton_);

    titleLabel_.setText(juce::String(AppConfig::appName.data()), juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel_.setFont(juce::FontOptions(22.0f, juce::Font::bold));

    headlineLabel_.setJustificationType(juce::Justification::centredLeft);
    headlineLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.94f));
    headlineLabel_.setFont(juce::FontOptions(17.0f, juce::Font::bold));

    detailLabel_.setJustificationType(juce::Justification::topLeft);
    detailLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.70f));
    detailLabel_.setFont(juce::FontOptions(13.5f));

    auto styleButton = [](juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(36, 39, 46));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(58, 122, 214));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
    };
    styleButton(retryButton_);
    styleButton(continueFallbackButton_);
    styleButton(showDetailsButton_);

    retryButton_.onClick = [this]()
    {
        if (onRetry)
        {
            onRetry();
        }
    };
    continueFallbackButton_.onClick = [this]()
    {
        if (onContinueFallback)
        {
            onContinueFallback();
        }
    };
    showDetailsButton_.onClick = [this]()
    {
        if (onShowDetails)
        {
            onShowDetails();
        }
    };
}

void SplashContent::resized()
{
    auto area = getLocalBounds().reduced(18);
    titleLabel_.setBounds(area.removeFromTop(34));
    area.removeFromTop(8);
    headlineLabel_.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);
    detailLabel_.setBounds(area.removeFromTop(74));
    area.removeFromTop(8);

    auto buttons = area.removeFromTop(34);
    retryButton_.setBounds(buttons.removeFromLeft(110).reduced(0, 2));
    buttons.removeFromLeft(8);
    continueFallbackButton_.setBounds(buttons.removeFromLeft(180).reduced(0, 2));
    buttons.removeFromLeft(8);
    showDetailsButton_.setBounds(buttons.removeFromLeft(120).reduced(0, 2));
}

void SplashContent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(15, 17, 21));
    g.setColour(juce::Colour::fromRGB(30, 34, 40));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 10.0f, 1.0f);

    auto progressArea = getLocalBounds().reduced(18).removeFromBottom(18).toFloat();
    g.setColour(juce::Colour::fromRGB(28, 31, 36));
    g.fillRoundedRectangle(progressArea, 4.0f);
    g.setColour(juce::Colour::fromRGB(58, 122, 214));
    g.fillRoundedRectangle(progressArea.removeFromLeft(progressArea.getWidth() * 0.42f), 4.0f);
}

void SplashContent::setStatus(const BootstrapStatus& status)
{
    headlineLabel_.setText(status.headline, juce::dontSendNotification);
    detailLabel_.setText(status.detail, juce::dontSendNotification);
    retryButton_.setVisible(status.showRetry);
    continueFallbackButton_.setVisible(status.showContinueFallback);
    showDetailsButton_.setVisible(status.showDetails);
    resized();
    repaint();
}

SplashWindow::SplashWindow()
    : juce::DocumentWindow(
          juce::String(AppConfig::appName.data()),
          juce::Colour::fromRGB(15, 17, 21),
          juce::DocumentWindow::closeButton)
    , content_(std::make_unique<SplashContent>())
{
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    setContentOwned(content_.release(), true);
    setSize(560, 240);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);

    auto* splashContent = dynamic_cast<SplashContent*>(getContentComponent());
    splashContent->onRetry = [this]()
    {
        if (onRetry)
        {
            onRetry();
        }
    };
    splashContent->onContinueFallback = [this]()
    {
        if (onContinueFallback)
        {
            onContinueFallback();
        }
    };
    splashContent->onShowDetails = [this]()
    {
        if (onShowDetails)
        {
            onShowDetails();
        }
    };
}

void SplashWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void SplashWindow::setStatus(const BootstrapStatus& status)
{
    if (auto* splashContent = dynamic_cast<SplashContent*>(getContentComponent()))
    {
        splashContent->setStatus(status);
    }
}
}
