#include "ModelManagerPanel.h"

#if MOON_HAS_JUCE

#include <algorithm>
#include <cctype>
#include <cmath>

namespace moon::ui
{
namespace
{
juce::Colour panelFill()      { return juce::Colour::fromRGB(17, 20, 25); }
juce::Colour panelOutline()   { return juce::Colour::fromRGB(41, 46, 54); }
juce::Colour controlFill()    { return juce::Colour::fromRGB(31, 36, 43); }
juce::Colour selectionFill()  { return juce::Colour::fromRGB(47, 89, 136); }
juce::Colour accentFill()     { return juce::Colour::fromRGB(43, 169, 237); }

std::string lowercaseCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void styleButton(juce::TextButton& button, bool accent = false)
{
    button.setColour(juce::TextButton::buttonColourId, accent ? accentFill() : controlFill());
    button.setColour(juce::TextButton::buttonOnColourId, accent ? accentFill() : controlFill());
    button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white.withAlpha(0.92f));
}

void styleCombo(juce::ComboBox& combo)
{
    combo.setColour(juce::ComboBox::backgroundColourId, controlFill());
    combo.setColour(juce::ComboBox::outlineColourId, panelOutline());
    combo.setColour(juce::ComboBox::textColourId, juce::Colours::white.withAlpha(0.92f));
    combo.setColour(juce::ComboBox::arrowColourId, juce::Colours::white.withAlpha(0.82f));
}
}

ModelManagerPanel::ModelManagerPanel()
{
    for (auto* button : {&installedTab_, &availableTab_, &localTab_, &refreshButton_, &syncRemoteButton_, &setActiveButton_, &downloadButton_, &updateButton_, &verifyButton_, &removeButton_, &addExistingButton_, &openFolderButton_})
    {
        addAndMakeVisible(button);
        styleButton(*button, button == &setActiveButton_ || button == &downloadButton_);
    }

    addAndMakeVisible(listBox_);
    addAndMakeVisible(capabilityFilterBox_);
    addAndMakeVisible(sourceFilterBox_);
    addAndMakeVisible(headerLabel_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(selectedOperationLabel_);
    addAndMakeVisible(selectedOperationProgressBar_);
    addAndMakeVisible(detailsEditor_);

    headerLabel_.setJustificationType(juce::Justification::centredLeft);
    headerLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.96f));
    headerLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    headerLabel_.setText("Model Manager", juce::dontSendNotification);

    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.64f));
    statusLabel_.setText({}, juce::dontSendNotification);

    selectedOperationLabel_.setJustificationType(juce::Justification::centredLeft);
    selectedOperationLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
    selectedOperationLabel_.setFont(juce::FontOptions(11.5f));
    selectedOperationLabel_.setText({}, juce::dontSendNotification);

    selectedOperationProgressBar_.setColour(juce::ProgressBar::foregroundColourId, accentFill());
    selectedOperationProgressBar_.setColour(juce::ProgressBar::backgroundColourId, controlFill().darker(0.35f));
    selectedOperationProgressBar_.setVisible(false);
    selectedOperationLabel_.setVisible(false);

    detailsEditor_.setReadOnly(true);
    detailsEditor_.setMultiLine(true);
    detailsEditor_.setScrollbarsShown(true);
    detailsEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(20, 24, 30));
    detailsEditor_.setColour(juce::TextEditor::outlineColourId, panelOutline());
    detailsEditor_.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.90f));
    detailsEditor_.setFont(juce::FontOptions(12.0f));
    listBox_.setRowHeight(38);

    styleCombo(capabilityFilterBox_);
    styleCombo(sourceFilterBox_);
    capabilityFilterBox_.addItem("Preferred Capability", 1);
    capabilityFilterBox_.addItem("Any Capability", 2);
    capabilityFilterBox_.setSelectedId(1, juce::dontSendNotification);
    sourceFilterBox_.addItem("Any Source", 1);
    sourceFilterBox_.addItem("Hugging Face", 2);
    sourceFilterBox_.addItem("Local Catalog", 3);
    sourceFilterBox_.setSelectedId(1, juce::dontSendNotification);
    capabilityFilterBox_.onChange = [this] { refreshFilters(); };
    sourceFilterBox_.onChange = [this] { refreshFilters(); };

    installedTab_.onClick = [this] { setSection(Section::Installed); };
    availableTab_.onClick = [this] { setSection(Section::Available); };
    localTab_.onClick = [this] { setSection(Section::Local); };
    refreshButton_.onClick = [this]
    {
        runAction(
            [this](std::string& errorMessage)
            {
                return refreshCallback_ ? refreshCallback_(errorMessage) : false;
            },
            "Model registry refreshed");
    };
    syncRemoteButton_.onClick = [this]
    {
        runAction(
            [this](std::string& errorMessage)
            {
                return syncRemoteCatalogCallback_ ? syncRemoteCatalogCallback_(errorMessage) : false;
            },
            "Hugging Face catalog synced");
    };
    setActiveButton_.onClick = [this]
    {
        const auto modelId = selectedModelId();
        if (modelId.empty())
        {
            return;
        }

        runAction(
            [this, modelId](std::string& errorMessage)
            {
                return setActiveCallback_ ? setActiveCallback_(preferredCapability_, modelId, errorMessage) : false;
            },
            "Active model updated");
    };
    downloadButton_.onClick = [this]
    {
        const auto modelId = selectedModelId();
        if (modelId.empty())
        {
            return;
        }

        runAction(
            [this, modelId](std::string& errorMessage)
            {
                return downloadCallback_ ? downloadCallback_(modelId, errorMessage) : false;
            },
            "Model download started; runtime will prepare automatically");
    };
    updateButton_.onClick = [this]
    {
        const auto modelId = selectedModelId();
        if (modelId.empty())
        {
            return;
        }

        runAction(
            [this, modelId](std::string& errorMessage)
            {
                return updateCallback_ ? updateCallback_(modelId, errorMessage) : false;
            },
            "Model update started; runtime will prepare automatically");
    };
    verifyButton_.onClick = [this]
    {
        const auto modelId = selectedModelId();
        if (modelId.empty())
        {
            return;
        }

        runAction(
            [this, modelId](std::string& errorMessage)
            {
                return verifyCallback_ ? verifyCallback_(modelId, errorMessage) : false;
            },
            "Model verified");
    };
    removeButton_.onClick = [this]
    {
        const auto modelId = selectedModelId();
        if (modelId.empty())
        {
            return;
        }

        runAction(
            [this, modelId](std::string& errorMessage)
            {
                return removeCallback_ ? removeCallback_(modelId, errorMessage) : false;
            },
            "Model removed");
    };
    addExistingButton_.onClick = [this]
    {
        if (activeSection_ != Section::Local)
        {
            promptAddExistingFolder();
            return;
        }

        const auto row = listBox_.getSelectedRow();
        const auto indexes = filteredRowIndexes();
        if (row < 0 || row >= static_cast<int>(indexes.size()))
        {
            statusLabel_.setText("Select a local model folder first", juce::dontSendNotification);
            return;
        }

        const auto& folder = snapshot_.localFolders[static_cast<std::size_t>(indexes[static_cast<std::size_t>(row)])];
        if (!folder.valid)
        {
            statusLabel_.setText("This local folder is not a valid model yet", juce::dontSendNotification);
            return;
        }

        const auto modelId = inferredModelIdForLocalFolder(folder);
        if (modelId.empty())
        {
            statusLabel_.setText("No compatible catalog model found for this local folder", juce::dontSendNotification);
            return;
        }

        runAction(
            [this, modelId, folder](std::string& errorMessage)
            {
                if (!addExistingCallback_ || !addExistingCallback_(modelId, folder.path.string(), errorMessage))
                {
                    return false;
                }

                if (setActiveCallback_)
                {
                    std::string activationError;
                    if (!setActiveCallback_(preferredCapability_, modelId, activationError) && errorMessage.empty())
                    {
                        errorMessage = activationError;
                    }
                }
                return true;
            },
            "Local model installed and activated");
    };
    addExistingButton_.setButtonText("Add Existing...");
    openFolderButton_.onClick = [this]
    {
        if (openModelsFolderCallback_)
        {
            openModelsFolderCallback_();
        }
    };

    setSection(Section::Installed);
    updateStatusSummary();
    startTimerHz(5);
}

ModelManagerPanel::~ModelManagerPanel()
{
    if (closeCallback_)
    {
        closeCallback_();
    }
}

void ModelManagerPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(panelFill());
    g.fillRoundedRectangle(bounds, 12.0f);
    g.setColour(panelOutline());
    g.drawRoundedRectangle(bounds, 12.0f, 1.0f);
}

void ModelManagerPanel::resized()
{
    auto area = getLocalBounds().reduced(16, 14);
    auto top = area.removeFromTop(24);
    headerLabel_.setBounds(top.removeFromLeft(220));
    statusLabel_.setBounds(top);

    area.removeFromTop(8);
    auto tabs = area.removeFromTop(28);
    auto filterArea = activeSection_ == Section::Local ? juce::Rectangle<int>{} : tabs.removeFromRight(activeSection_ == Section::Available ? 304 : 156);
    installedTab_.setBounds(tabs.removeFromLeft(94));
    tabs.removeFromLeft(6);
    availableTab_.setBounds(tabs.removeFromLeft(94));
    tabs.removeFromLeft(6);
    localTab_.setBounds(tabs.removeFromLeft(74));
    if (activeSection_ != Section::Local)
    {
        capabilityFilterBox_.setBounds(filterArea.removeFromLeft(156));
        filterArea.removeFromLeft(8);
        sourceFilterBox_.setBounds(activeSection_ == Section::Available ? filterArea.removeFromLeft(140) : juce::Rectangle<int>{});
    }
    else
    {
        capabilityFilterBox_.setBounds({});
        sourceFilterBox_.setBounds({});
    }
    area.removeFromTop(8);
    auto footer = area.removeFromBottom(34);
    auto left = area.removeFromLeft(320);
    left.removeFromRight(8);
    listBox_.setBounds(left);

    auto right = area;
    auto buttonRow = footer;
    auto placeButton = [&buttonRow](juce::TextButton& button, int width)
    {
        if (!button.isVisible())
        {
            button.setBounds({});
            return;
        }

        button.setBounds(buttonRow.removeFromLeft(width));
        buttonRow.removeFromLeft(6);
    };

    placeButton(refreshButton_, 82);
    placeButton(syncRemoteButton_, 76);
    placeButton(setActiveButton_, 92);
    placeButton(downloadButton_, 90);
    placeButton(updateButton_, 82);
    placeButton(verifyButton_, 82);
    placeButton(removeButton_, 82);
    placeButton(addExistingButton_, 146);
    placeButton(openFolderButton_, 138);

    auto progressArea = right.removeFromTop(0);
    if (selectedOperationProgressBar_.isVisible())
    {
        progressArea = right.removeFromTop(34);
        selectedOperationLabel_.setBounds(progressArea.removeFromTop(14));
        progressArea.removeFromTop(6);
        selectedOperationProgressBar_.setBounds(progressArea.removeFromTop(10));
        right.removeFromTop(8);
    }
    else
    {
        selectedOperationLabel_.setBounds({});
        selectedOperationProgressBar_.setBounds({});
    }

    detailsEditor_.setBounds(right);
}

void ModelManagerPanel::setSnapshot(const moon::engine::ModelRegistrySnapshot& snapshot)
{
    const auto preferredModelId = selectedModelId();
    snapshot_ = snapshot;
    listBox_.updateContent();
    restoreSelection(preferredModelId);
    refreshDetails();
    updateStatusSummary();
}

void ModelManagerPanel::setPreferredCapability(moon::engine::ModelCapability capability)
{
    const auto preferredModelId = selectedModelId();
    preferredCapability_ = capability;
    restoreSelection(preferredModelId);
    refreshDetails();
    updateStatusSummary();
}

void ModelManagerPanel::setRefreshCallback(std::function<bool(std::string&)> callback) { refreshCallback_ = std::move(callback); }
void ModelManagerPanel::setPollCallback(std::function<bool(std::string&)> callback) { pollCallback_ = std::move(callback); }
void ModelManagerPanel::setSyncRemoteCatalogCallback(std::function<bool(std::string&)> callback) { syncRemoteCatalogCallback_ = std::move(callback); }
void ModelManagerPanel::setSetActiveCallback(std::function<bool(moon::engine::ModelCapability, const std::string&, std::string&)> callback) { setActiveCallback_ = std::move(callback); }
void ModelManagerPanel::setDownloadCallback(std::function<bool(const std::string&, std::string&)> callback) { downloadCallback_ = std::move(callback); }
void ModelManagerPanel::setUpdateCallback(std::function<bool(const std::string&, std::string&)> callback) { updateCallback_ = std::move(callback); }
void ModelManagerPanel::setVerifyCallback(std::function<bool(const std::string&, std::string&)> callback) { verifyCallback_ = std::move(callback); }
void ModelManagerPanel::setRemoveCallback(std::function<bool(const std::string&, std::string&)> callback) { removeCallback_ = std::move(callback); }
void ModelManagerPanel::setAddExistingCallback(std::function<bool(const std::string&, const std::string&, std::string&)> callback) { addExistingCallback_ = std::move(callback); }
void ModelManagerPanel::setOpenModelsFolderCallback(std::function<void()> callback) { openModelsFolderCallback_ = std::move(callback); }
void ModelManagerPanel::setCloseCallback(std::function<void()> callback) { closeCallback_ = std::move(callback); }

int ModelManagerPanel::getNumRows()
{
    return static_cast<int>(filteredRowIndexes().size());
}

void ModelManagerPanel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto indexes = filteredRowIndexes();
    if (rowNumber < 0 || rowNumber >= static_cast<int>(indexes.size()))
    {
        return;
    }
    const auto sourceRow = indexes[static_cast<std::size_t>(rowNumber)];

    g.setColour(rowIsSelected ? selectionFill() : controlFill().darker(0.2f));
    g.fillRoundedRectangle(2.0f, 2.0f, static_cast<float>(width - 4), static_cast<float>(height - 4), 8.0f);

    juce::String title;
    juce::String meta;
    double progress = 0.0;
    if (activeSection_ == Section::Installed)
    {
        const auto& item = snapshot_.installed[static_cast<std::size_t>(sourceRow)];
        title = item.displayName.empty() ? juce::String(item.id) : juce::String(item.displayName);
        meta = item.operationStatusText.empty()
            ? (juce::String(std::string(moon::engine::modelStatusLabel(item.status))) + "  |  " + juce::String(item.version))
            : (juce::String(item.operationStatusText) + "  |  " + juce::String(static_cast<int>(std::round(item.operationProgress * 100.0))) + "%");
        progress = item.operationProgress;
    }
    else if (activeSection_ == Section::Available)
    {
        const auto& item = snapshot_.available[static_cast<std::size_t>(sourceRow)];
        title = item.displayName.empty() ? juce::String(item.id) : juce::String(item.displayName);
        meta = item.operationStatusText.empty()
            ? (item.version == "remote" ? juce::String("Remote") : juce::String(item.version))
            : (juce::String(item.operationStatusText) + "  |  " + juce::String(static_cast<int>(std::round(item.operationProgress * 100.0))) + "%");
        progress = item.operationProgress;
    }
    else
    {
        const auto& item = snapshot_.localFolders[static_cast<std::size_t>(sourceRow)];
        title = juce::String(item.detectedName);
        meta = item.valid ? juce::String("Usable") : juce::String("Broken");
    }

    if (meta.isNotEmpty())
    {
        const auto pillWidth = juce::jlimit(74, 164, 24 + static_cast<int>(meta.length()) * 6);
        const auto pillBounds = juce::Rectangle<float>(static_cast<float>(width - pillWidth - 10), 7.0f, static_cast<float>(pillWidth), 22.0f);
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(title, 12, 0, width - pillWidth - 28, height - (progress > 0.0 && progress < 1.0 ? 6 : 0), juce::Justification::centredLeft);
        g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.16f) : juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(pillBounds, 11.0f);
        g.setColour(juce::Colours::white.withAlpha(0.72f));
        g.setFont(juce::FontOptions(10.3f));
        g.drawText(meta, pillBounds.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(title, 12, 0, width - 24, height - (progress > 0.0 && progress < 1.0 ? 6 : 0), juce::Justification::centredLeft);
    }

    if (progress > 0.0 && progress < 1.0)
    {
        const auto barBounds = juce::Rectangle<float>(12.0f, static_cast<float>(height - 10), static_cast<float>(width - 24), 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.30f));
        g.fillRoundedRectangle(barBounds, 2.0f);
        g.setColour(accentFill());
        g.fillRoundedRectangle(barBounds.withWidth(barBounds.getWidth() * static_cast<float>(progress)), 2.0f);
    }
}

void ModelManagerPanel::selectedRowsChanged(int)
{
    refreshDetails();
}

void ModelManagerPanel::timerCallback()
{
    if (!pollCallback_)
    {
        return;
    }

    std::string errorMessage;
    const bool changed = pollCallback_(errorMessage);
    if (!errorMessage.empty())
    {
        statusLabel_.setText(juce::String(errorMessage), juce::dontSendNotification);
        return;
    }

    if (changed)
    {
        updateStatusSummary();
    }
}

void ModelManagerPanel::setSection(Section section)
{
    const auto preferredModelId = selectedModelId();
    activeSection_ = section;
    installedTab_.setToggleState(section == Section::Installed, juce::dontSendNotification);
    availableTab_.setToggleState(section == Section::Available, juce::dontSendNotification);
    localTab_.setToggleState(section == Section::Local, juce::dontSendNotification);
    listBox_.updateContent();
    restoreSelection(preferredModelId);
    refreshDetails();
    updateStatusSummary();
}

void ModelManagerPanel::refreshFilters()
{
    const auto preferredModelId = selectedModelId();
    capabilityFilter_ = capabilityFilterBox_.getSelectedId() == 2 ? CapabilityFilter::Any : CapabilityFilter::Preferred;
    sourceFilter_ = sourceFilterBox_.getSelectedId() == 2 ? SourceFilter::HuggingFace
        : (sourceFilterBox_.getSelectedId() == 3 ? SourceFilter::LocalCatalog : SourceFilter::Any);
    listBox_.updateContent();
    restoreSelection(preferredModelId);
    refreshDetails();
    updateStatusSummary();
}

void ModelManagerPanel::refreshDetails()
{
    detailsEditor_.setText(detailTitleForRow(listBox_.getSelectedRow()) + "\n\n" + detailBodyForRow(listBox_.getSelectedRow()), juce::dontSendNotification);
    updateSelectedOperationDisplay();

    const auto modelId = selectedModelId();
    const bool hasModel = !modelId.empty();
    const bool installedSection = activeSection_ == Section::Installed;
    const bool availableSection = activeSection_ == Section::Available;
    const bool localSection = activeSection_ == Section::Local;
    const bool hasLocalFolder = !selectedLocalFolderPath().empty();
    const bool localFolderValid = selectedLocalFolderValid();
    bool selectedAvailableModelCanDownload = false;

    if (availableSection && hasModel)
    {
        for (const auto& item : snapshot_.available)
        {
            if (item.id == modelId)
            {
                selectedAvailableModelCanDownload = !item.downloadUri.empty();
                break;
            }
        }
    }

    refreshButton_.setVisible(true);
    syncRemoteButton_.setVisible(activeSection_ == Section::Available);
    setActiveButton_.setVisible(installedSection);
    downloadButton_.setVisible(availableSection);
    updateButton_.setVisible(false);
    verifyButton_.setVisible(installedSection);
    removeButton_.setVisible(installedSection);
    addExistingButton_.setVisible(!installedSection);
    openFolderButton_.setVisible(true);

    addExistingButton_.setButtonText(localSection ? "Use Local Model" : "Add Existing...");
    syncRemoteButton_.setEnabled(true);
    setActiveButton_.setEnabled(hasModel && installedSection);
    downloadButton_.setEnabled(hasModel && availableSection && selectedAvailableModelCanDownload);
    updateButton_.setEnabled(hasModel && installedSection);
    verifyButton_.setEnabled(hasModel && installedSection);
    removeButton_.setEnabled(hasModel && installedSection);
    addExistingButton_.setEnabled(localSection ? (hasLocalFolder && localFolderValid) : hasModel);
    capabilityFilterBox_.setEnabled(activeSection_ != Section::Local);
    capabilityFilterBox_.setVisible(activeSection_ != Section::Local);
    sourceFilterBox_.setEnabled(activeSection_ == Section::Available);
    sourceFilterBox_.setVisible(activeSection_ == Section::Available);
    resized();
}

void ModelManagerPanel::updateSelectedOperationDisplay()
{
    selectedOperationProgress_ = 0.0;
    juce::String text;
    bool shouldShow = false;

    const auto row = listBox_.getSelectedRow();
    const auto indexes = filteredRowIndexes();
    if (row >= 0 && row < static_cast<int>(indexes.size()))
    {
        const auto sourceRow = indexes[static_cast<std::size_t>(row)];
        if (activeSection_ == Section::Installed)
        {
            const auto& item = snapshot_.installed[static_cast<std::size_t>(sourceRow)];
            if (!item.operationStatusText.empty())
            {
                shouldShow = true;
                selectedOperationProgress_ = item.operationProgress;
                text = juce::String(item.operationStatusText)
                    + "  "
                    + juce::String(static_cast<int>(std::round(item.operationProgress * 100.0)))
                    + "%";
            }
        }
        else if (activeSection_ == Section::Available)
        {
            const auto& item = snapshot_.available[static_cast<std::size_t>(sourceRow)];
            if (!item.operationStatusText.empty())
            {
                shouldShow = true;
                selectedOperationProgress_ = item.operationProgress;
                text = juce::String(item.operationStatusText)
                    + "  "
                    + juce::String(static_cast<int>(std::round(item.operationProgress * 100.0)))
                    + "%";
            }
        }
    }

    selectedOperationLabel_.setVisible(shouldShow);
    selectedOperationProgressBar_.setVisible(shouldShow);
    selectedOperationLabel_.setText(text, juce::dontSendNotification);
}

void ModelManagerPanel::updateStatusSummary()
{
    int inProgressCount = 0;
    for (const auto& item : snapshot_.installed)
    {
        if (!item.operationStatusText.empty())
        {
            ++inProgressCount;
        }
    }
    for (const auto& item : snapshot_.available)
    {
        if (!item.operationStatusText.empty())
        {
            ++inProgressCount;
        }
    }

    juce::String summary;
    if (inProgressCount > 0)
    {
        summary = juce::String(inProgressCount) + " active";
    }
    else if (activeSection_ == Section::Installed)
    {
        summary = juce::String(snapshot_.installed.size()) + " installed";
    }
    else if (activeSection_ == Section::Available)
    {
        summary = juce::String(filteredRowIndexes().size()) + " available";
    }
    else
    {
        summary = juce::String(snapshot_.localFolders.size()) + " local folders";
    }

    statusLabel_.setText(summary, juce::dontSendNotification);
}

juce::String ModelManagerPanel::detailTitleForRow(int row) const
{
    const auto indexes = filteredRowIndexes();
    if (row < 0 || row >= static_cast<int>(indexes.size()))
    {
        return "No item selected";
    }
    const auto sourceRow = indexes[static_cast<std::size_t>(row)];

    if (activeSection_ == Section::Installed)
    {
        const auto& item = snapshot_.installed[static_cast<std::size_t>(sourceRow)];
        return item.displayName.empty() ? juce::String(item.id) : juce::String(item.displayName);
    }
    if (activeSection_ == Section::Available)
    {
        const auto& item = snapshot_.available[static_cast<std::size_t>(sourceRow)];
        return item.displayName.empty() ? juce::String(item.id) : juce::String(item.displayName);
    }

    return juce::String(snapshot_.localFolders[static_cast<std::size_t>(sourceRow)].detectedName);
}

juce::String ModelManagerPanel::detailBodyForRow(int row) const
{
    const auto indexes = filteredRowIndexes();
    if (row < 0 || row >= static_cast<int>(indexes.size()))
    {
        if (activeSection_ == Section::Local)
        {
            return "Local models are folders inside the models/install area.\n\n"
                   "If a folder appears here, select it and press Use Local Model. "
                   "Moon will register it as installed and activate it for generation.\n\n"
                   "If the list is empty, press Open Models Folder and put the model folder into installs/ or use Add Existing from Available.";
        }

        if (activeSection_ == Section::Installed)
        {
            return "No installed model is selected.\n\n"
                   "Open Local to use an already downloaded folder, or Available to download/add a model.";
        }

        return "No catalog model is selected.\n\n"
               "Choose a model to download, or use Add Existing if you already have the files locally.";
    }
    const auto sourceRow = indexes[static_cast<std::size_t>(row)];

    juce::StringArray lines;
    if (activeSection_ == Section::Installed)
    {
        const auto& item = snapshot_.installed[static_cast<std::size_t>(sourceRow)];
        lines.add("ID: " + juce::String(item.id));
        lines.add("Version: " + juce::String(item.version));
        lines.add("Status: " + juce::String(std::string(moon::engine::modelStatusLabel(item.status))));
        if (!item.operationStatusText.empty())
        {
            lines.add("Operation: " + juce::String(item.operationStatusText));
        }
        lines.add("Path: " + item.installPath.string());
        lines.add("Source: " + juce::String(item.source));
        lines.add("Active Binding: " + juce::String(item.selectedForGeneration ? "yes" : "no"));
        lines.add("Installed: " + juce::String(item.installedAt));
    }
    else if (activeSection_ == Section::Available)
    {
        const auto& item = snapshot_.available[static_cast<std::size_t>(sourceRow)];
        lines.add("ID: " + juce::String(item.id));
        if (!item.remoteId.empty())
        {
            lines.add("Remote ID: " + juce::String(item.remoteId));
        }
        lines.add("Provider: " + juce::String(item.provider));
        lines.add("Version: " + juce::String(item.version));
        lines.add("Source: " + juce::String(item.source));
        if (!item.operationStatusText.empty())
        {
            lines.add("Operation: " + juce::String(item.operationStatusText));
            lines.add("Progress: " + juce::String(static_cast<int>(std::round(item.operationProgress * 100.0))) + "%");
        }
        if (!item.lastModified.empty())
        {
            lines.add("Last Modified: " + juce::String(item.lastModified));
        }
        lines.add("Approx Size: " + (item.approximateSizeMb > 0
            ? (juce::String(static_cast<int>(item.approximateSizeMb)) + " MB")
            : juce::String("Unknown")));
        if (!item.homepageUrl.empty())
        {
            lines.add("Homepage: " + juce::String(item.homepageUrl));
        }
        lines.add("Download: " + juce::String(item.downloadUri.empty() ? "not available, use Add Existing Folder" : "available"));
        lines.add("Description: " + juce::String(item.description));
    }
    else
    {
        const auto& item = snapshot_.localFolders[static_cast<std::size_t>(sourceRow)];
        const auto inferredId = inferredModelIdForLocalFolder(item);
        lines.add(item.valid ? "This folder looks usable." : "This folder is not usable yet.");
        lines.add("");
        lines.add("Action:");
        lines.add(item.valid
            ? "Press Use Local Model to register it as installed and make it active."
            : "Fix the folder files first. Moon needs real model weights, not only metadata/config files.");
        lines.add("");
        lines.add("Detected Model: " + juce::String(item.detectedName));
        if (!inferredId.empty())
        {
            lines.add("Will Register As: " + juce::String(inferredId));
        }
        lines.add("Folder: " + item.path.string());
        lines.add("Check: " + juce::String(item.statusNote));
    }

    return lines.joinIntoString("\n");
}

std::string ModelManagerPanel::selectedModelId() const
{
    const auto row = listBox_.getSelectedRow();
    const auto indexes = filteredRowIndexes();
    if (row < 0 || row >= static_cast<int>(indexes.size()))
    {
        return {};
    }
    const auto sourceRow = indexes[static_cast<std::size_t>(row)];

    if (activeSection_ == Section::Installed)
    {
        return snapshot_.installed[static_cast<std::size_t>(sourceRow)].id;
    }
    if (activeSection_ == Section::Available)
    {
        return snapshot_.available[static_cast<std::size_t>(sourceRow)].id;
    }
    return {};
}

std::filesystem::path ModelManagerPanel::selectedLocalFolderPath() const
{
    if (activeSection_ != Section::Local)
    {
        return {};
    }

    const auto row = listBox_.getSelectedRow();
    const auto indexes = filteredRowIndexes();
    if (row < 0 || row >= static_cast<int>(indexes.size()))
    {
        return {};
    }

    return snapshot_.localFolders[static_cast<std::size_t>(indexes[static_cast<std::size_t>(row)])].path;
}

bool ModelManagerPanel::selectedLocalFolderValid() const
{
    if (activeSection_ != Section::Local)
    {
        return false;
    }

    const auto row = listBox_.getSelectedRow();
    const auto indexes = filteredRowIndexes();
    if (row < 0 || row >= static_cast<int>(indexes.size()))
    {
        return false;
    }

    return snapshot_.localFolders[static_cast<std::size_t>(indexes[static_cast<std::size_t>(row)])].valid;
}

std::string ModelManagerPanel::inferredModelIdForLocalFolder(const moon::engine::LocalModelFolderInfo& folder) const
{
    const auto folderName = lowercaseCopy(folder.detectedName + " " + folder.path.filename().string());
    const auto scoreDescriptor = [&folderName, this](const moon::engine::ModelDescriptor& descriptor)
    {
        int score = 0;
        const auto id = lowercaseCopy(descriptor.id);
        const auto name = lowercaseCopy(descriptor.displayName);
        const auto version = lowercaseCopy(descriptor.version);

        if (std::find(descriptor.capabilities.begin(), descriptor.capabilities.end(), preferredCapability_) != descriptor.capabilities.end())
        {
            score += 20;
        }
        if (!id.empty() && folderName.find(id) != std::string::npos)
        {
            score += 50;
        }
        if (!name.empty() && folderName.find(name) != std::string::npos)
        {
            score += 40;
        }
        if (!version.empty() && folderName.find(version) != std::string::npos)
        {
            score += 20;
        }
        if (folderName.find("xl") != std::string::npos && (id.find("xl") != std::string::npos || name.find("xl") != std::string::npos))
        {
            score += 12;
        }
        if (folderName.find("turbo") != std::string::npos && (id.find("turbo") != std::string::npos || name.find("turbo") != std::string::npos))
        {
            score += 12;
        }
        if ((folderName.find("1.5") != std::string::npos || folderName.find("15") != std::string::npos)
            && (id.find("15") != std::string::npos || id.find("1.5") != std::string::npos || name.find("1.5") != std::string::npos))
        {
            score += 10;
        }
        if (folderName.find("ace") != std::string::npos && (id.find("ace") != std::string::npos || name.find("ace") != std::string::npos))
        {
            score += 8;
        }
        return score;
    };

    const moon::engine::ModelDescriptor* best = nullptr;
    int bestScore = 0;
    for (const auto& descriptor : snapshot_.available)
    {
        const auto score = scoreDescriptor(descriptor);
        if (score > bestScore)
        {
            best = &descriptor;
            bestScore = score;
        }
    }

    if (best != nullptr && bestScore > 0)
    {
        return best->id;
    }

    for (const auto& descriptor : snapshot_.available)
    {
        if (std::find(descriptor.capabilities.begin(), descriptor.capabilities.end(), preferredCapability_) != descriptor.capabilities.end())
        {
            return descriptor.id;
        }
    }

    return snapshot_.available.empty() ? std::string{} : snapshot_.available.front().id;
}

std::vector<int> ModelManagerPanel::filteredRowIndexes() const
{
    std::vector<int> indexes;
    if (activeSection_ == Section::Installed)
    {
        for (std::size_t index = 0; index < snapshot_.installed.size(); ++index)
        {
            const auto& item = snapshot_.installed[index];
            if (capabilityFilter_ == CapabilityFilter::Preferred
                && std::find(item.capabilities.begin(), item.capabilities.end(), preferredCapability_) == item.capabilities.end())
            {
                continue;
            }
            indexes.push_back(static_cast<int>(index));
        }
        return indexes;
    }

    if (activeSection_ == Section::Available)
    {
        for (std::size_t index = 0; index < snapshot_.available.size(); ++index)
        {
            const auto& item = snapshot_.available[index];
            if (capabilityFilter_ == CapabilityFilter::Preferred
                && std::find(item.capabilities.begin(), item.capabilities.end(), preferredCapability_) == item.capabilities.end())
            {
                continue;
            }
            if (sourceFilter_ == SourceFilter::HuggingFace && item.source != "huggingface")
            {
                continue;
            }
            if (sourceFilter_ == SourceFilter::LocalCatalog && item.source == "huggingface")
            {
                continue;
            }
            indexes.push_back(static_cast<int>(index));
        }
        return indexes;
    }

    for (std::size_t index = 0; index < snapshot_.localFolders.size(); ++index)
    {
        indexes.push_back(static_cast<int>(index));
    }
    return indexes;
}

void ModelManagerPanel::restoreSelection(const std::string& preferredModelId)
{
    const auto indexes = filteredRowIndexes();
    if (indexes.empty())
    {
        listBox_.selectRow(-1);
        return;
    }

    if (!preferredModelId.empty())
    {
        for (std::size_t row = 0; row < indexes.size(); ++row)
        {
            const auto sourceRow = indexes[row];
            if (activeSection_ == Section::Installed
                && snapshot_.installed[static_cast<std::size_t>(sourceRow)].id == preferredModelId)
            {
                listBox_.selectRow(static_cast<int>(row));
                return;
            }

            if (activeSection_ == Section::Available
                && snapshot_.available[static_cast<std::size_t>(sourceRow)].id == preferredModelId)
            {
                listBox_.selectRow(static_cast<int>(row));
                return;
            }
        }
    }

    const auto currentRow = listBox_.getSelectedRow();
    if (currentRow >= 0 && currentRow < static_cast<int>(indexes.size()))
    {
        listBox_.selectRow(currentRow);
        return;
    }

    listBox_.selectRow(0);
}

void ModelManagerPanel::runAction(const std::function<bool(std::string&)>& action, const juce::String& successText)
{
    std::string errorMessage;
    if (!action || !action(errorMessage))
    {
        statusLabel_.setText(errorMessage.empty() ? "Operation failed" : juce::String(errorMessage), juce::dontSendNotification);
        return;
    }

    statusLabel_.setText(successText, juce::dontSendNotification);
    std::string refreshError;
    if (refreshCallback_)
    {
        refreshCallback_(refreshError);
    }
}

void ModelManagerPanel::promptAddExistingFolder()
{
    std::string modelId = selectedModelId();
    if (activeSection_ == Section::Available && !modelId.empty())
    {
        launchExistingFolderChooser(modelId);
        return;
    }

    juce::PopupMenu menu;
    int itemId = 1;
    std::vector<std::string> candidateModelIds;
    for (const auto& item : snapshot_.available)
    {
        if (std::find(item.capabilities.begin(), item.capabilities.end(), preferredCapability_) == item.capabilities.end())
        {
            continue;
        }

        candidateModelIds.push_back(item.id);
        menu.addItem(itemId++, juce::String(item.displayName.empty() ? item.id : item.displayName));
    }

    if (candidateModelIds.empty())
    {
        statusLabel_.setText("No catalog model matches the current generation capability", juce::dontSendNotification);
        return;
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&addExistingButton_),
        [this, candidateModelIds](int result)
        {
            if (result <= 0 || result > static_cast<int>(candidateModelIds.size()))
            {
                return;
            }

            launchExistingFolderChooser(candidateModelIds[static_cast<std::size_t>(result - 1)]);
        });
}

void ModelManagerPanel::launchExistingFolderChooser(const std::string& modelId)
{
    activeFileChooser_ = std::make_unique<juce::FileChooser>("Choose Existing Model Folder");
    activeFileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, modelId](const juce::FileChooser& chooser)
        {
            const auto folder = chooser.getResult();
            activeFileChooser_.reset();
            if (folder == juce::File())
            {
                return;
            }

            runAction(
                [this, modelId, folder](std::string& errorMessage)
                {
                    if (!addExistingCallback_ || !addExistingCallback_(modelId, folder.getFullPathName().toStdString(), errorMessage))
                    {
                        return false;
                    }

                    if (setActiveCallback_)
                    {
                        std::string activationError;
                        if (!setActiveCallback_(preferredCapability_, modelId, activationError) && errorMessage.empty())
                        {
                            errorMessage = activationError;
                        }
                    }
                    return true;
                },
                "Existing model folder installed and activated");
        });
}
}
#endif
