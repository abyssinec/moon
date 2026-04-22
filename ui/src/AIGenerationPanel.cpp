#include "AIGenerationPanel.h"

#if MOON_HAS_JUCE

#include <algorithm>
#include <cmath>

namespace moon::ui
{
namespace
{
constexpr int kCollapsedHeight = 60;
constexpr int kExpandedHeight = 230;

juce::Colour panelFill()                     { return juce::Colour::fromRGB(15, 19, 24); }
juce::Colour panelOutline()                  { return juce::Colour::fromRGB(40, 46, 54); }
juce::Colour controlFill()                   { return juce::Colour::fromRGB(28, 33, 40); }
juce::Colour controlOutline()                { return juce::Colour::fromRGB(54, 60, 69); }
juce::Colour editorFill()                    { return juce::Colour::fromRGB(17, 21, 27); }
juce::Colour createFill()                    { return juce::Colour::fromRGB(43, 169, 237); }
juce::Colour createFillBusy()                { return juce::Colour::fromRGB(78, 110, 136); }

void styleSecondaryButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, controlFill());
    button.setColour(juce::TextButton::buttonOnColourId, controlFill());
    button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white.withAlpha(0.92f));
}

void stylePromptEditor(juce::TextEditor& editor, const juce::String& placeholder)
{
    editor.setMultiLine(true);
    editor.setReturnKeyStartsNewLine(true);
    editor.setScrollbarsShown(true);
    editor.setPopupMenuEnabled(true);
    editor.setIndents(10, 10);
    editor.setFont(juce::FontOptions(13.2f));
    editor.setColour(juce::TextEditor::backgroundColourId, editorFill());
    editor.setColour(juce::TextEditor::outlineColourId, controlOutline());
    editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour::fromRGB(86, 150, 227));
    editor.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.95f));
    editor.setColour(juce::TextEditor::highlightColourId, juce::Colour::fromRGB(71, 110, 183).withAlpha(0.56f));
    editor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    editor.setColour(juce::CaretComponent::caretColourId, juce::Colours::white);
    editor.setTextToShowWhenEmpty(placeholder, juce::Colours::white.withAlpha(0.30f));
}

juce::String modelDisplayName(const moon::engine::InstalledModelInfo* model)
{
    if (model == nullptr)
    {
        return "No model";
    }

    if (!model->displayName.empty())
    {
        return juce::String(model->displayName);
    }

    return juce::String(model->id);
}

const moon::engine::InstalledModelInfo* findInstalledModel(const moon::engine::ModelRegistrySnapshot& snapshot, const std::string& modelId)
{
    const auto it = std::find_if(
        snapshot.installed.begin(),
        snapshot.installed.end(),
        [&modelId](const moon::engine::InstalledModelInfo& item)
        {
            return item.id == modelId;
        });
    return it == snapshot.installed.end() ? nullptr : &(*it);
}
}

AIGenerationPanel::AIGenerationPanel()
{
    addAndMakeVisible(expandButton_);
    addAndMakeVisible(modelButton_);
    addAndMakeVisible(manageModelsButton_);
    addAndMakeVisible(targetButton_);
    addAndMakeVisible(deviceButton_);
    addAndMakeVisible(createButton_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(modelCaptionLabel_);
    addAndMakeVisible(stylesLabel_);
    addAndMakeVisible(secondaryLabel_);
    addAndMakeVisible(stylesEditor_);
    addAndMakeVisible(secondaryEditor_);

    styleSecondaryButton(expandButton_);
    styleSecondaryButton(modelButton_);
    styleSecondaryButton(manageModelsButton_);
    styleSecondaryButton(targetButton_);
    styleSecondaryButton(deviceButton_);

    createButton_.setColour(juce::TextButton::buttonColourId, createFill());
    createButton_.setColour(juce::TextButton::buttonOnColourId, createFill());
    createButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    createButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    expandButton_.onClick = [this] { toggleExpanded(); };
    modelButton_.onClick = [this] { showModelMenu(); };
    manageModelsButton_.onClick = [this]
    {
        if (openModelManagerCallback_)
        {
            openModelManagerCallback_(currentCapability());
        }
    };
    targetButton_.onClick = [this] { showTargetMenu(); };
    deviceButton_.onClick = [this] { showDeviceMenu(); };
    createButton_.onClick = [this] { submitCreate(); };

    modelCaptionLabel_.setText("Model", juce::dontSendNotification);
    modelCaptionLabel_.setJustificationType(juce::Justification::centredLeft);
    modelCaptionLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.60f));
    modelCaptionLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setInterceptsMouseClicks(false, false);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.68f));

    stylesLabel_.setText("Style Prompt", juce::dontSendNotification);
    secondaryLabel_.setText("Lyrics", juce::dontSendNotification);
    for (auto* label : {&stylesLabel_, &secondaryLabel_})
    {
        label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.84f));
        label->setFont(juce::FontOptions(12.5f, juce::Font::bold));
    }

    stylePromptEditor(stylesEditor_, "Describe genre, mood, instrumentation, production style, era, texture...");
    stylePromptEditor(secondaryEditor_, "Write lyrics for the song...");

    stylesEditor_.onTextChange = [this]
    {
        if (!generating_)
        {
            refreshControls();
        }
    };
    secondaryEditor_.onTextChange = [this]
    {
        if (!generating_)
        {
            storeSecondaryPromptForCurrentTarget();
            refreshControls();
        }
    };

    secondaryPromptByTarget_[selectedTarget_] = {};
    updateSecondaryProfile();
    refreshControls();
}

void AIGenerationPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(4.0f, 2.0f);
    g.setColour(panelFill());
    g.fillRoundedRectangle(bounds, 11.0f);
    g.setColour(panelOutline());
    g.drawRoundedRectangle(bounds, 11.0f, 1.0f);

    auto header = bounds;
    header.removeFromBottom(std::max(0.0f, bounds.getHeight() - 50.0f));
    g.setColour(juce::Colour::fromRGB(22, 27, 33));
    g.fillRoundedRectangle(header, 11.0f);

    if (!expanded_)
    {
        return;
    }

    auto content = bounds.reduced(12.0f, 12.0f);
    content.removeFromTop(42.0f);
    auto columns = content;
    auto left = columns.removeFromLeft(columns.getWidth() * 0.5f - 4.0f);
    auto right = columns.reduced(8.0f, 0.0f);

    g.setColour(controlOutline().withAlpha(0.42f));
    g.drawRoundedRectangle(left.toFloat().reduced(-1.0f, -1.0f), 10.0f, 1.0f);
    g.drawRoundedRectangle(right.toFloat().reduced(-1.0f, -1.0f), 10.0f, 1.0f);
}

void AIGenerationPanel::resized()
{
    auto area = getLocalBounds().reduced(12, 10);
    auto header = area.removeFromTop(34);

    expandButton_.setBounds(header.removeFromLeft(30));
    header.removeFromLeft(8);
    modelCaptionLabel_.setBounds(header.removeFromLeft(44));
    modelButton_.setBounds(header.removeFromLeft(180));
    header.removeFromLeft(8);
    manageModelsButton_.setBounds(header.removeFromLeft(126));
    header.removeFromLeft(8);
    targetButton_.setBounds(header.removeFromLeft(146));
    header.removeFromLeft(8);
    deviceButton_.setBounds(header.removeFromLeft(86));
    header.removeFromRight(8);
    createButton_.setBounds(header.removeFromRight(112));
    header.removeFromRight(8);
    statusLabel_.setBounds(header);

    const bool showExpanded = expanded_;
    stylesLabel_.setVisible(showExpanded);
    secondaryLabel_.setVisible(showExpanded);
    stylesEditor_.setVisible(showExpanded);
    secondaryEditor_.setVisible(showExpanded);

    if (!showExpanded)
    {
        return;
    }

    area.removeFromTop(10);
    auto columns = area;
    auto left = columns.removeFromLeft(columns.getWidth() / 2).reduced(0, 0);
    columns.removeFromLeft(8);
    auto right = columns;

    stylesLabel_.setBounds(left.removeFromTop(18));
    left.removeFromTop(4);
    stylesEditor_.setBounds(left);

    secondaryLabel_.setBounds(right.removeFromTop(18));
    right.removeFromTop(4);
    secondaryEditor_.setBounds(right);
}

void AIGenerationPanel::setCreateCallback(std::function<MusicGenerationSubmission(const moon::engine::MusicGenerationRequest&)> callback)
{
    createCallback_ = std::move(callback);
}

void AIGenerationPanel::setModelRegistrySnapshot(const moon::engine::ModelRegistrySnapshot& snapshot)
{
    modelRegistrySnapshot_ = snapshot;
    refreshControls();
}

void AIGenerationPanel::setModelSelectionCallback(std::function<bool(moon::engine::ModelCapability, const std::string&, std::string&)> callback)
{
    modelSelectionCallback_ = std::move(callback);
}

void AIGenerationPanel::setOpenModelManagerCallback(std::function<void(moon::engine::ModelCapability)> callback)
{
    openModelManagerCallback_ = std::move(callback);
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
        statusText_ = "Generated clip added to timeline";
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

void AIGenerationPanel::showTargetMenu()
{
    juce::PopupMenu menu;
    int itemId = 1;
    for (const auto target : moon::engine::kGenerationTargets)
    {
        menu.addItem(
            itemId++,
            juce::String(std::string(moon::engine::musicGenerationCategoryLabel(target))),
            true,
            target == selectedTarget_);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&targetButton_),
        [this](int result)
        {
            if (result <= 0 || result > static_cast<int>(moon::engine::kGenerationTargets.size()))
            {
                return;
            }

            storeSecondaryPromptForCurrentTarget();
            selectedTarget_ = moon::engine::kGenerationTargets[static_cast<std::size_t>(result - 1)];
            restoreSecondaryPromptForCurrentTarget();
            updateSecondaryProfile();
            refreshControls();
        });
}

void AIGenerationPanel::showDeviceMenu()
{
    juce::PopupMenu menu;
    const std::array<moon::engine::ComputeDevicePreference, 3> options{
        moon::engine::ComputeDevicePreference::Auto,
        moon::engine::ComputeDevicePreference::GPU,
        moon::engine::ComputeDevicePreference::CPU,
    };

    int itemId = 1;
    for (const auto option : options)
    {
        menu.addItem(
            itemId++,
            juce::String(std::string(moon::engine::computeDevicePreferenceLabel(option))),
            true,
            option == selectedDevicePreference_);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&deviceButton_),
        [this, options](int result)
        {
            if (result <= 0 || result > static_cast<int>(options.size()))
            {
                return;
            }

            selectedDevicePreference_ = options[static_cast<std::size_t>(result - 1)];
            refreshControls();
        });
}

void AIGenerationPanel::showModelMenu()
{
    juce::PopupMenu menu;
    const auto capability = currentCapability();
    const auto activeModel = activeModelId();

    int itemId = 1;
    for (const auto& model : modelRegistrySnapshot_.installed)
    {
        if (model.status != moon::engine::ModelStatus::Ready && model.status != moon::engine::ModelStatus::UpdateAvailable)
        {
            continue;
        }

        if (std::find(model.capabilities.begin(), model.capabilities.end(), capability) == model.capabilities.end())
        {
            continue;
        }

        auto label = juce::String(model.displayName.empty() ? model.id : model.displayName);
        if (!model.version.empty())
        {
            label << "  v" << model.version;
        }
        menu.addItem(itemId++, label, true, model.id == activeModel);
    }

    if (itemId == 1)
    {
        menu.addItem(1, "No ready models for this target", false, false);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&modelButton_),
        [this, capability](int result)
        {
            if (result <= 0)
            {
                return;
            }

            std::vector<std::string> candidateIds;
            for (const auto& model : modelRegistrySnapshot_.installed)
            {
                if ((model.status == moon::engine::ModelStatus::Ready || model.status == moon::engine::ModelStatus::UpdateAvailable)
                    && std::find(model.capabilities.begin(), model.capabilities.end(), capability) != model.capabilities.end())
                {
                    candidateIds.push_back(model.id);
                }
            }

            if (result > static_cast<int>(candidateIds.size()) || candidateIds.empty())
            {
                return;
            }

            std::string errorMessage;
            if (modelSelectionCallback_ && !modelSelectionCallback_(capability, candidateIds[static_cast<std::size_t>(result - 1)], errorMessage))
            {
                statusText_ = errorMessage.empty() ? juce::String("Could not select model") : juce::String(errorMessage);
            }
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
        statusText_ = "Enter style or notes";
        refreshControls();
        return;
    }

    if (activeModelId().empty())
    {
        statusText_ = "Select a ready model first";
        refreshControls();
        return;
    }

    if (!createCallback_)
    {
        statusText_ = "Generation callback is not configured";
        refreshControls();
        return;
    }

    const auto submission = createCallback_(buildRequest());
    if (!submission.accepted || submission.jobId.empty())
    {
        statusText_ = submission.errorMessage.empty() ? juce::String("Generation could not start") : juce::String(submission.errorMessage);
        refreshControls();
        return;
    }

    activeJobId_ = submission.jobId;
    generating_ = true;
    statusText_ = "Generating...";
    refreshControls();
}

void AIGenerationPanel::refreshControls()
{
    expandButton_.setButtonText(expanded_ ? "v" : ">");
    targetButton_.setButtonText(currentTargetLabel());
    deviceButton_.setButtonText(currentDeviceLabel());
    modelButton_.setButtonText(activeModelLabel());
    statusLabel_.setText(activeModelStatusLabel().isNotEmpty() ? (activeModelStatusLabel() + "  |  " + statusText_) : statusText_, juce::dontSendNotification);
    createButton_.setButtonText(generating_ ? "Generating..." : "Create");
    createButton_.setColour(juce::TextButton::buttonColourId, generating_ ? createFillBusy() : createFill());
    manageModelsButton_.setButtonText(expanded_ ? "Model Manager" : "Models...");

    const bool hasReadyModel = !activeModelId().empty();
    const bool canCreate = !generating_ && hasReadyModel && !requestLooksEmpty();
    createButton_.setEnabled(canCreate);
    modelButton_.setEnabled(!generating_);
    manageModelsButton_.setEnabled(!generating_);
    targetButton_.setEnabled(!generating_);
    deviceButton_.setEnabled(!generating_);
    stylesEditor_.setReadOnly(generating_);
    secondaryEditor_.setReadOnly(generating_);
    repaint();
}

void AIGenerationPanel::updateSecondaryProfile()
{
    const auto& profile = currentTargetProfile();
    secondaryLabel_.setText(juce::String(std::string(profile.secondaryLabel)), juce::dontSendNotification);
    secondaryEditor_.setTextToShowWhenEmpty(
        juce::String(std::string(profile.secondaryPlaceholder)),
        juce::Colours::white.withAlpha(0.30f));
}

void AIGenerationPanel::storeSecondaryPromptForCurrentTarget()
{
    secondaryPromptByTarget_[selectedTarget_] = secondaryEditor_.getText().toStdString();
}

void AIGenerationPanel::restoreSecondaryPromptForCurrentTarget()
{
    const auto it = secondaryPromptByTarget_.find(selectedTarget_);
    secondaryEditor_.setText(it == secondaryPromptByTarget_.end() ? juce::String{} : juce::String(it->second), juce::dontSendNotification);
}

moon::engine::MusicGenerationRequest AIGenerationPanel::buildRequest() const
{
    moon::engine::MusicGenerationRequest request;
    request.category = selectedTarget_;
    request.devicePreference = selectedDevicePreference_;
    request.stylesPrompt = stylesEditor_.getText().trim().toStdString();
    request.secondaryPrompt = secondaryEditor_.getText().trim().toStdString();
    request.secondaryPromptIsLyrics = currentTargetProfile().secondaryPromptRepresentsLyrics;
    request.lyricsPrompt = request.secondaryPromptIsLyrics ? request.secondaryPrompt : std::string{};
    request.isInstrumental = !request.secondaryPromptIsLyrics || request.secondaryPrompt.empty();
    request.selectedModel = activeModelId();
    request.selectedModelDisplayName = activeModelLabel().toStdString();
    request.selectedModelVersion = activeModelVersion();
    request.selectedModelPath = activeModelPath();
    return request;
}

bool AIGenerationPanel::requestLooksEmpty() const
{
    return stylesEditor_.getText().trim().isEmpty() && secondaryEditor_.getText().trim().isEmpty();
}

juce::String AIGenerationPanel::currentTargetLabel() const
{
    return juce::String(std::string(moon::engine::musicGenerationCategoryLabel(selectedTarget_)));
}

juce::String AIGenerationPanel::currentDeviceLabel() const
{
    return juce::String(std::string(moon::engine::computeDevicePreferenceLabel(selectedDevicePreference_)));
}

juce::String AIGenerationPanel::activeModelLabel() const
{
    return modelDisplayName(findInstalledModel(modelRegistrySnapshot_, activeModelId()));
}

juce::String AIGenerationPanel::activeModelStatusLabel() const
{
    const auto* model = findInstalledModel(modelRegistrySnapshot_, activeModelId());
    if (model == nullptr)
    {
        return "No model";
    }

    juce::String text{std::string(moon::engine::modelStatusLabel(model->status))};
    if (!model->version.empty())
    {
        text += "  v";
        text += juce::String(model->version);
    }
    return text;
}

std::string AIGenerationPanel::activeModelId() const
{
    const auto capability = currentCapability();
    const auto it = modelRegistrySnapshot_.activeBindings.find(capability);
    if (it == modelRegistrySnapshot_.activeBindings.end())
    {
        return {};
    }
    return it->second;
}

std::string AIGenerationPanel::activeModelVersion() const
{
    if (const auto* model = findInstalledModel(modelRegistrySnapshot_, activeModelId()))
    {
        return model->version;
    }
    return {};
}

std::string AIGenerationPanel::activeModelPath() const
{
    if (const auto* model = findInstalledModel(modelRegistrySnapshot_, activeModelId()))
    {
        return model->installPath.string();
    }
    return {};
}

moon::engine::ModelCapability AIGenerationPanel::currentCapability() const noexcept
{
    return currentTargetProfile().capability;
}

moon::engine::GenerationTargetProfile AIGenerationPanel::currentTargetProfile() const noexcept
{
    return moon::engine::generationTargetProfile(selectedTarget_);
}
}
#endif
