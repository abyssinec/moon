#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ModelManager.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class ModelManagerPanel final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    enum class Section
    {
        Installed,
        Available,
        Local
    };

    ModelManagerPanel();
    ~ModelManagerPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setSnapshot(const moon::engine::ModelRegistrySnapshot& snapshot);
    void setPreferredCapability(moon::engine::ModelCapability capability);
    void setRefreshCallback(std::function<bool(std::string&)> callback);
    void setPollCallback(std::function<bool(std::string&)> callback);
    void setSyncRemoteCatalogCallback(std::function<bool(std::string&)> callback);
    void setSetActiveCallback(std::function<bool(moon::engine::ModelCapability, const std::string&, std::string&)> callback);
    void setDownloadCallback(std::function<bool(const std::string&, std::string&)> callback);
    void setUpdateCallback(std::function<bool(const std::string&, std::string&)> callback);
    void setVerifyCallback(std::function<bool(const std::string&, std::string&)> callback);
    void setRemoveCallback(std::function<bool(const std::string&, std::string&)> callback);
    void setAddExistingCallback(std::function<bool(const std::string&, const std::string&, std::string&)> callback);
    void setOpenModelsFolderCallback(std::function<void()> callback);
    void setCloseCallback(std::function<void()> callback);

private:
    enum class CapabilityFilter
    {
        Preferred,
        Any
    };

    enum class SourceFilter
    {
        Any,
        HuggingFace,
        LocalCatalog
    };

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void timerCallback() override;

    void setSection(Section section);
    void refreshFilters();
    void refreshDetails();
    void updateStatusSummary();
    void updateSelectedOperationDisplay();
    juce::String detailTitleForRow(int row) const;
    juce::String detailBodyForRow(int row) const;
    std::string selectedModelId() const;
    std::filesystem::path selectedLocalFolderPath() const;
    bool selectedLocalFolderValid() const;
    std::string inferredModelIdForLocalFolder(const moon::engine::LocalModelFolderInfo& folder) const;
    std::vector<int> filteredRowIndexes() const;
    void restoreSelection(const std::string& preferredModelId);
    void runAction(const std::function<bool(std::string&)>& action, const juce::String& successText);
    void promptAddExistingFolder();
    void launchExistingFolderChooser(const std::string& modelId);

    moon::engine::ModelRegistrySnapshot snapshot_;
    moon::engine::ModelCapability preferredCapability_{moon::engine::ModelCapability::SongGeneration};
    Section activeSection_{Section::Installed};
    CapabilityFilter capabilityFilter_{CapabilityFilter::Preferred};
    SourceFilter sourceFilter_{SourceFilter::Any};
    double selectedOperationProgress_{0.0};

    std::function<bool(std::string&)> refreshCallback_;
    std::function<bool(std::string&)> pollCallback_;
    std::function<bool(std::string&)> syncRemoteCatalogCallback_;
    std::function<bool(moon::engine::ModelCapability, const std::string&, std::string&)> setActiveCallback_;
    std::function<bool(const std::string&, std::string&)> downloadCallback_;
    std::function<bool(const std::string&, std::string&)> updateCallback_;
    std::function<bool(const std::string&, std::string&)> verifyCallback_;
    std::function<bool(const std::string&, std::string&)> removeCallback_;
    std::function<bool(const std::string&, const std::string&, std::string&)> addExistingCallback_;
    std::function<void()> openModelsFolderCallback_;
    std::function<void()> closeCallback_;

    juce::TextButton installedTab_{"Installed"};
    juce::TextButton availableTab_{"Available"};
    juce::TextButton localTab_{"Local"};
    juce::ComboBox capabilityFilterBox_;
    juce::ComboBox sourceFilterBox_;
    juce::ListBox listBox_{"models", this};
    juce::Label headerLabel_;
    juce::Label statusLabel_;
    juce::Label selectedOperationLabel_;
    juce::ProgressBar selectedOperationProgressBar_{selectedOperationProgress_};
    juce::TextEditor detailsEditor_;
    juce::TextButton refreshButton_{"Refresh"};
    juce::TextButton syncRemoteButton_{"Sync HF"};
    juce::TextButton setActiveButton_{"Set Active"};
    juce::TextButton downloadButton_{"Download"};
    juce::TextButton updateButton_{"Update"};
    juce::TextButton verifyButton_{"Verify"};
    juce::TextButton removeButton_{"Remove"};
    juce::TextButton addExistingButton_{"Add Existing Folder..."};
    juce::TextButton openFolderButton_{"Open Models Folder"};
    std::unique_ptr<juce::FileChooser> activeFileChooser_;
};
}
#else
namespace moon::ui
{
class ModelManagerPanel
{
public:
    ModelManagerPanel() = default;
};
}
#endif
