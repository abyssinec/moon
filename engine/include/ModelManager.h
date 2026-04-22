#pragma once

#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Logger.h"
#include "MusicGeneration.h"

namespace moon::engine
{
enum class ModelStatus
{
    NotInstalled,
    Downloading,
    Downloaded,
    Verifying,
    RuntimeMissing,
    RuntimePreparing,
    Ready,
    Running,
    Broken,
    Incompatible,
    Failed,
    UpdateAvailable,
    Removing
};

inline constexpr std::string_view modelStatusLabel(ModelStatus status) noexcept
{
    switch (status)
    {
    case ModelStatus::NotInstalled:   return "Not Installed";
    case ModelStatus::Downloading:    return "Downloading";
    case ModelStatus::Downloaded:     return "Downloaded";
    case ModelStatus::Verifying:      return "Verifying";
    case ModelStatus::RuntimeMissing: return "Runtime Missing";
    case ModelStatus::RuntimePreparing:return "Preparing Runtime";
    case ModelStatus::Ready:          return "Ready";
    case ModelStatus::Running:        return "Running";
    case ModelStatus::Broken:         return "Broken";
    case ModelStatus::Incompatible:   return "Incompatible";
    case ModelStatus::Failed:         return "Failed";
    case ModelStatus::UpdateAvailable:return "Update Available";
    case ModelStatus::Removing:       return "Removing";
    }

    return "Not Installed";
}

struct ModelDescriptor
{
    std::string id;
    std::string displayName;
    std::string version;
    std::string source;
    std::string provider;
    std::string description;
    std::string downloadUri;
    std::string remoteId;
    std::string homepageUrl;
    std::string lastModified;
    std::uint64_t approximateSizeMb{0};
    bool supportsExistingFolder{true};
    std::vector<ModelCapability> capabilities;
    double operationProgress{0.0};
    std::string operationStatusText;
};

struct InstalledModelInfo
{
    std::string id;
    std::string displayName;
    std::string version;
    std::filesystem::path installPath;
    std::string source;
    ModelStatus status{ModelStatus::NotInstalled};
    std::vector<ModelCapability> capabilities;
    std::string installedAt;
    bool selectedForGeneration{false};
    double operationProgress{0.0};
    std::string operationStatusText;
};

struct LocalModelFolderInfo
{
    std::filesystem::path path;
    std::string detectedName;
    std::string statusNote;
    bool valid{false};
};

struct ModelRegistrySnapshot
{
    std::vector<InstalledModelInfo> installed;
    std::vector<ModelDescriptor> available;
    std::vector<LocalModelFolderInfo> localFolders;
    std::map<ModelCapability, std::string> activeBindings;
};

struct ResolvedModelInfo
{
    std::string id;
    std::string displayName;
    std::string version;
    std::filesystem::path installPath;
    std::vector<ModelCapability> capabilities;
    ModelStatus status{ModelStatus::NotInstalled};
};

struct ModelInstallManifest
{
    std::string modelId;
    std::string remoteId;
    std::string version;
    std::filesystem::path installPath;
    std::vector<std::string> files;
};

struct ModelOperationState
{
    ModelStatus status{ModelStatus::NotInstalled};
    double progress{0.0};
    std::string message;
    bool cancelled{false};
    mutable std::mutex mutex;

    void setProgress(ModelStatus newStatus, double newProgress, std::string newMessage)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = newStatus;
        progress = newProgress;
        message = std::move(newMessage);
    }

    void requestCancel()
    {
        std::lock_guard<std::mutex> lock(mutex);
        cancelled = true;
        message = "Cancelling...";
    }

    bool isCancelled() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return cancelled;
    }
};

struct ModelOperationResult
{
    bool success{false};
    bool cancelled{false};
    std::string modelId;
    std::string errorMessage;
    std::optional<InstalledModelInfo> installedRecord;
};

class ModelManager
{
public:
    explicit ModelManager(Logger& logger, std::filesystem::path rootPath = std::filesystem::path("data") / "models");

    bool refresh(std::string* errorMessage = nullptr);
    bool syncRemoteCatalog(std::string* errorMessage = nullptr);
    bool pollOperations(std::string* errorMessage = nullptr);
    ModelRegistrySnapshot snapshot() const;

    const std::filesystem::path& rootPath() const noexcept { return rootPath_; }
    std::filesystem::path catalogDirectory() const noexcept { return catalogDirectory_; }
    std::filesystem::path installsDirectory() const noexcept { return installsDirectory_; }
    std::filesystem::path cacheDirectory() const noexcept { return cacheDirectory_; }
    std::filesystem::path manifestsDirectory() const noexcept { return manifestsDirectory_; }

    std::vector<ModelDescriptor> modelsForCapability(ModelCapability capability) const;
    std::optional<ResolvedModelInfo> resolveActiveModel(ModelCapability capability) const;
    std::optional<ResolvedModelInfo> resolveInstalledModel(const std::string& modelId) const;

    bool setActiveModel(ModelCapability capability, const std::string& modelId, std::string& errorMessage);
    bool addExistingModelFolder(const std::string& modelId, const std::filesystem::path& folderPath, std::string& errorMessage);
    bool verifyModel(const std::string& modelId, std::string& errorMessage);
    bool removeModel(const std::string& modelId, std::string& errorMessage);
    bool downloadModel(const std::string& modelId, std::string& errorMessage);
    bool updateModel(const std::string& modelId, std::string& errorMessage);
    bool cancelModelOperation(const std::string& modelId, std::string& errorMessage);
    bool cancelAllModelOperations(std::string* errorMessage = nullptr);

private:
    void ensureDirectories() const;
    void seedCatalogIfNeeded();
    bool loadCatalog(std::string* errorMessage);
    bool loadRegistry(std::string* errorMessage);
    bool saveRegistry(std::string* errorMessage) const;
    std::vector<LocalModelFolderInfo> scanLocalFolders() const;
    bool validateModelFolder(const std::filesystem::path& folderPath, std::string& errorMessage) const;
    bool syncHuggingFaceCatalog(std::string* errorMessage);
    bool writeCatalogDescriptor(const ModelDescriptor& descriptor, std::string* errorMessage) const;
    std::filesystem::path catalogPathForModelId(const std::string& modelId) const;
    std::filesystem::path installManifestPath(const std::filesystem::path& installPath) const;
    bool loadInstallManifest(const std::filesystem::path& installPath, ModelInstallManifest& manifest) const;
    bool saveInstallManifest(const ModelInstallManifest& manifest, std::string* errorMessage) const;
    ModelOperationResult performDownloadOperation(ModelDescriptor descriptor, std::shared_ptr<ModelOperationState> operationState) const;
    std::optional<ModelDescriptor> findCatalogModel(const std::string& modelId) const;
    InstalledModelInfo makeInstalledRecord(const ModelDescriptor& descriptor,
                                           const std::filesystem::path& installPath,
                                           const std::string& source,
                                           ModelStatus status) const;
    void reconcileInstalledStatuses();

    Logger& logger_;
    std::filesystem::path rootPath_;
    std::filesystem::path catalogDirectory_;
    std::filesystem::path installsDirectory_;
    std::filesystem::path cacheDirectory_;
    std::filesystem::path manifestsDirectory_;
    std::filesystem::path registryPath_;
    std::vector<ModelDescriptor> catalog_;
    std::map<std::string, InstalledModelInfo> installed_;
    std::map<ModelCapability, std::string> activeBindings_;
    mutable std::mutex operationsMutex_;
    std::map<std::string, std::shared_ptr<ModelOperationState>> operations_;
    std::map<std::string, std::future<ModelOperationResult>> operationFutures_;
};
}
