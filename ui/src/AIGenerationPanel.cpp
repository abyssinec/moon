#include "AIGenerationPanel.h"

#if MOON_HAS_JUCE

#include <algorithm>
#include <cmath>

namespace moon::ui
{
namespace
{
constexpr int kCollapsedHeight = 58;
constexpr int kExpandedHeight = 214;

void styleSecondaryButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(34, 37, 43));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(34, 37, 43));
    button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white.withAlpha(0.92f));
}

void stylePromptEditor(juce::TextEditor& editor, const juce::String& placeholder)
{
    editor.setMultiLine(true);
    editor.setReturnKeyStartsNewLine(true);
    editor.setScrollbarsShown(true);
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(18, 22, 28));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(46, 51, 59));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour::fromRGB(74, 126, 184));
    editor.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.94f));
    editor.setColour(juce::TextEditor::highlightColourId, juce::Colour::fromRGB(56, 102, 171).withAlpha(0.58f));
    editor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    editor.setColour(juce::CaretComponent::caretColourId, juce::Colours::white);
    editor.setTextToShowWhenEmpty(placeholder, juce::Colours::white.withAlpha(0.28f));
    editor.setPopupMenuEnabled(true);
    editor.setIndents(10, 10);
    editor.setFont(juce::FontOptions(13.0f));
}

juce::String displayModelName(const std::string& model)
{
    if (model == "ace_step_api")
    {
        return "Acestep API";
    }
    if (model == "ace_step")
    {
        return "Acestep";
    }
    return juce::String(model);
}
}

AIGenerationPanel::AIGenerationPanel()
{
    addAndMakeVisible(expandButton_);
    addAndMakeVisible(categoryButton_);
    addAndMakeVisible(modelButton_);
    addAndMakeVisible(createButton_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(stylesLabel_);
    addAndMakeVisible(lyricsLabel_);
    addAndMakeVisible(stylesEditor_);
    addAndMakeVisible(lyricsEditor_);

    styleSecondaryButton(expandButton_);
    styleSecondaryButton(categoryButton_);
    styleSecondaryButton(modelButton_);

    expandButton_.setClickingTogglesState(false);
    expandButton_.onClick = [this] { toggleExpanded(); };
    categoryButton_.onClick = [this] { showCategoryMenu(); };
    modelButton_.onClick = [this] { showModelMenu(); };
    createButton_.onClick = [this] { submitCreate(); };

    createButton_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(43, 169, 237));
    createButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(32, 144, 205));
    createButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    createButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.68f));
    statusLabel_.setInterceptsMouseClicks(false, false);

    stylesLabel_.setText("Styles", juce::dontSendNotification);
    lyricsLabel_.setText("Lyrics", juce::dontSendNotification);
    stylesLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
    lyricsLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
    stylesLabel_.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    lyricsLabel_.setFont(juce::FontOptions(12.5f, juce::Font::bold));

    stylePromptEditor(stylesEditor_, "Describe style, genre, mood, instrumentation, production, tempo, references...");
    stylePromptEditor(lyricsEditor_, "Write some lyrics or leave blank for instrumental");
    stylesEditor_.onTextChange = [this]
    {
        if (!generating_)
        {
            if (requestLooksEmpty())
            {
                statusText_ = availableModels_.empty() ? juce::String("Acestep unavailable") : juce::String("Enter styles or lyrics");
            }
            else if (activeJobId_.empty())
            {
                statusText_ = juce::String("Ready to create");
            }
            refreshControls();
        }
    };
    lyricsEditor_.onTextChange = stylesEditor_.onTextChange;

    refreshControls();
}

void AIGenerationPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(4.0f, 2.0f);
    g.setColour(juce::Colour::fromRGB(16, 19, 24));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(juce::Colour::fromRGB(41, 46, 54));
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(23, 27, 33));
    g.fillRoundedRectangle(bounds.removeFromTop(52.0f), 10.0f);
}

void AIGenerationPanel::resized()
{
    auto area = getLocalBounds().reduced(12, 10);
    auto header = area.removeFromTop(34);

    expandButton_.setBounds(header.removeFromLeft(34));
    header.removeFromLeft(8);
    categoryButton_.setBounds(header.removeFromLeft(148));
    header.removeFromLeft(8);
    modelButton_.setBounds(header.removeFromLeft(144));
    header.removeFromLeft(10);
    createButton_.setBounds(header.removeFromRight(118));
    header.removeFromRight(8);
    statusLabel_.setBounds(header);

    const bool showExpanded = expanded_;
    stylesLabel_.setVisible(showExpanded);
    lyricsLabel_.setVisible(showExpanded);
    stylesEditor_.setVisible(showExpanded);
    lyricsEditor_.setVisible(showExpanded);

    if (!showExpanded)
    {
        return;
    }

    area.removeFromTop(10);
    auto columns = area;
    auto left = columns.removeFromLeft(columns.getWidth() / 2).reduced(0, 0);
    auto right = columns.reduced(0, 0);

    stylesLabel_.setBounds(left.removeFromTop(18));
    left.removeFromTop(4);
    stylesEditor_.setBounds(left);

    lyricsLabel_.setBounds(right.removeFromTop(18));
    right.removeFromTop(4);
    lyricsEditor_.setBounds(right);
}

void AIGenerationPanel::setCreateCallback(std::function<MusicGenerationSubmission(const moon::engine::MusicGenerationRequest&)> callback)
{
    createCallback_ = std::move(callback);
}

void AIGenerationPanel::setAvailableModels(const std::vector<std::string>& models)
{
    availableModels_ = models;
    if (selectedModel_.empty() || std::find(availableModels_.begin(), availableModels_.end(), selectedModel_) == availableModels_.end())
    {
        selectedModel_ = availableModels_.empty() ? std::string{} : availableModels_.front();
    }

    if (!generating_)
    {
        if (availableModels_.empty())
        {
            statusText_ = juce::String("Acestep unavailable");
        }
        else if (requestLooksEmpty())
        {
            statusText_ = juce::String("Enter styles or lyrics");
        }
        else
        {
            statusText_ = juce::String("Ready to create");
        }
    }

    refreshControls();
}

void AIGenerationPanel::refreshTaskState(const moon::engine::TaskManager& taskManager)
{
    if (activeJobId_.empty())
    {
        refreshControls();
        return;
    }

    const auto tasks = taskManager.tasks();
    const auto it = tasks.find(activeJobId_);
    if (it == tasks.end())
    {
        refreshControls();
        return;
    }

    const auto& task = it->second;
    if (task.status == "queued" || task.status == "running")
    {
        generating_ = true;
        statusText_ = juce::String("Generating... ") + juce::String(static_cast<int>(std::round(task.progress * 100.0))) + "%";
    }
    else if (task.status == "completed")
    {
        generating_ = false;
        activeJobId_.clear();
        statusText_ = juce::String("Generated clip added to timeline");
    }
    else if (task.status == "failed")
    {
        generating_ = false;
        activeJobId_.clear();
        statusText_ = task.message.empty() ? juce::String("Generation failed") : juce::String(task.message);
    }

    refreshControls();
}

int AIGenerationPanel::preferredHeight() const noexcept
{
    return expanded_ ? kExpandedHeight : kCollapsedHeight;
}

void AIGenerationPanel::toggleExpanded()
{
    expanded_ = !expanded_;
    refreshControls();
    if (onHeightChanged)
    {
        onHeightChanged();
    }
}

void AIGenerationPanel::showCategoryMenu()
{
    juce::PopupMenu menu;
    int itemId = 1;
    for (const auto category : moon::engine::kMusicGenerationCategories)
    {
        menu.addItem(
            itemId++,
            juce::String(std::string(moon::engine::musicGenerationCategoryLabel(category))),
            true,
            category == selectedCategory_);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&categoryButton_),
        [this](int result)
        {
            if (result <= 0 || result > static_cast<int>(moon::engine::kMusicGenerationCategories.size()))
            {
                return;
            }

            selectedCategory_ = moon::engine::kMusicGenerationCategories[static_cast<std::size_t>(result - 1)];
            refreshControls();
        });
}

void AIGenerationPanel::showModelMenu()
{
    if (availableModels_.empty())
    {
        return;
    }

    juce::PopupMenu menu;
    for (std::size_t i = 0; i < availableModels_.size(); ++i)
    {
        menu.addItem(static_cast<int>(i + 1), displayModelName(availableModels_[i]), true, availableModels_[i] == selectedModel_);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&modelButton_),
        [this](int result)
        {
            if (result <= 0 || result > static_cast<int>(availableModels_.size()))
            {
                return;
            }

            selectedModel_ = availableModels_[static_cast<std::size_t>(result - 1)];
            refreshControls();
        });
}

void AIGenerationPanel::submitCreate()
{
    if (generating_)
    {
        return;
    }

    if (requestLooksEmpty())
    {
        statusText_ = juce::String("Enter styles or lyrics");
        refreshControls();
        return;
    }

    if (availableModels_.empty())
    {
        statusText_ = juce::String("Acestep unavailable");
        refreshControls();
        return;
    }

    if (!createCallback_)
    {
        statusText_ = juce::String("Music generation callback is not configured");
        refreshControls();
        return;
    }

    const auto request = buildRequest();
    const auto submission = createCallback_(request);
    if (!submission.accepted || submission.jobId.empty())
    {
        statusText_ = submission.errorMessage.empty() ? juce::String("Music generation could not start") : juce::String(submission.errorMessage);
        refreshControls();
        return;
    }

    activeJobId_ = submission.jobId;
    generating_ = true;
    statusText_ = juce::String("Generating...");
    refreshControls();
}

void AIGenerationPanel::refreshControls()
{
    expandButton_.setButtonText(expanded_ ? "v" : ">");
    categoryButton_.setButtonText(categoryLabel());
    modelButton_.setButtonText(modelLabel());
    statusLabel_.setText(statusText_, juce::dontSendNotification);
    createButton_.setButtonText(generating_ ? "Generating..." : "Create");

    const bool canCreate = !generating_ && !availableModels_.empty() && !requestLooksEmpty();
    createButton_.setEnabled(canCreate);
    modelButton_.setEnabled(!availableModels_.empty() && !generating_);
    categoryButton_.setEnabled(!generating_);
    stylesEditor_.setReadOnly(generating_);
    lyricsEditor_.setReadOnly(generating_);
    repaint();
}

moon::engine::MusicGenerationRequest AIGenerationPanel::buildRequest() const
{
    moon::engine::MusicGenerationRequest request;
    request.category = selectedCategory_;
    request.selectedModel = selectedModel_;
    request.stylesPrompt = stylesEditor_.getText().trim().toStdString();
    request.lyricsPrompt = lyricsEditor_.getText().trim().toStdString();
    request.isInstrumental = request.lyricsPrompt.empty();
    return request;
}

juce::String AIGenerationPanel::categoryLabel() const
{
    return juce::String(std::string(moon::engine::musicGenerationCategoryLabel(selectedCategory_)));
}

juce::String AIGenerationPanel::modelLabel() const
{
    if (availableModels_.empty())
    {
        return "No model";
    }

    return selectedModel_.empty() ? displayModelName(availableModels_.front()) : displayModelName(selectedModel_);
}

bool AIGenerationPanel::requestLooksEmpty() const
{
    return stylesEditor_.getText().trim().isEmpty() && lyricsEditor_.getText().trim().isEmpty();
}
}
#endif
