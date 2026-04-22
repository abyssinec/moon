#include "AppController.h"

#if MOON_HAS_JUCE
#include <juce_audio_formats/juce_audio_formats.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>

#include "AppConfig.h"

namespace moon::app
{
namespace
{
void appendBootstrapTrace(const std::string& line)
{
#if defined(_WIN32)
    if (const auto* localAppData = std::getenv("LOCALAPPDATA"))
    {
        const auto path = std::filesystem::path(localAppData) / "MoonAudioEditor" / "logs" / "bootstrap.log";
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::app);
        if (out)
        {
            out << line << "\n";
        }
        return;
    }
#endif
}

moon::engine::ClipInfo* findSelectedClip(moon::engine::ProjectState& state)
{
    for (auto& clip : state.clips)
    {
        if (clip.selected)
        {
            return &clip;
        }
    }
    return nullptr;
}

std::string resolveAssetPath(const moon::engine::ProjectState& state, const moon::engine::ClipInfo& clip)
{
    if (const auto sourceIt = state.sourceAssets.find(clip.assetId); sourceIt != state.sourceAssets.end())
    {
        return sourceIt->second.path;
    }
    if (const auto generatedIt = state.generatedAssets.find(clip.assetId); generatedIt != state.generatedAssets.end())
    {
        return generatedIt->second.path;
    }
    return {};
}

#if MOON_HAS_JUCE
struct AudioImportMetadata
{
    double durationSec{0.0};
    int sampleRate{0};
};

AudioImportMetadata readAudioImportMetadata(const std::string& audioPath)
{
    AudioImportMetadata metadata;
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(juce::File(audioPath)));
    if (reader == nullptr)
    {
        return metadata;
    }

    metadata.sampleRate = static_cast<int>(reader->sampleRate);
    if (reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
    {
        metadata.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    }
    return metadata;
}
#endif

void clearSelectedClipState(moon::engine::ProjectState& state)
{
    state.uiState.selectedClipId.clear();
    for (auto& clip : state.clips)
    {
        clip.selected = false;
    }
}

std::string trimCopy(std::string value)
{
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
    return value;
}

std::filesystem::path appRuntimeBasePath()
{
#if MOON_HAS_JUCE
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    if (executable.existsAsFile())
    {
        return executable.getParentDirectory().getFullPathName().toStdString();
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path modelRootPath()
{
    if (const auto* explicitRoot = std::getenv("MOON_MODELS_ROOT"))
    {
        const auto trimmed = trimCopy(explicitRoot);
        if (!trimmed.empty())
        {
            return std::filesystem::path(trimmed);
        }
    }

    const auto exeRoot = appRuntimeBasePath() / "data" / "models";
    if (std::filesystem::exists(exeRoot))
    {
        return exeRoot;
    }

    return std::filesystem::current_path() / "data" / "models";
}

std::filesystem::path backendRootPath()
{
    const auto executableDir = appRuntimeBasePath();
    const auto cwd = std::filesystem::current_path();
    std::vector<std::filesystem::path> candidates;

    const auto appendCandidate = [&candidates](const std::filesystem::path& candidate)
    {
        if (!candidate.empty())
        {
            candidates.push_back(candidate.lexically_normal());
        }
    };

    const auto appendFromAnchor = [&](std::filesystem::path anchor)
    {
        while (!anchor.empty())
        {
            appendCandidate(anchor / "backend");
            if (anchor.filename() == "backend")
            {
                appendCandidate(anchor);
            }
            if (!anchor.has_parent_path() || anchor.parent_path() == anchor)
            {
                break;
            }
            anchor = anchor.parent_path();
        }
    };

    appendCandidate(executableDir / "data" / "backend");
    if (executableDir.has_parent_path())
    {
        appendCandidate(executableDir.parent_path() / "data" / "backend");
    }
    appendFromAnchor(cwd);
    appendFromAnchor(executableDir);

    int bestScore = std::numeric_limits<int>::min();
    std::filesystem::path bestCandidate;
    for (const auto& candidate : candidates)
    {
        if (!std::filesystem::exists(candidate / "main.py"))
        {
            continue;
        }

        int score = 0;
        if (std::filesystem::exists(candidate / ".venv" / "Scripts" / "python.exe"))
        {
            score += 100;
        }
        if (std::filesystem::exists(candidate / "requirements.txt"))
        {
            score += 20;
        }
        if (std::filesystem::exists(candidate / "app"))
        {
            score += 10;
        }
        if (candidate == (executableDir / "data" / "backend").lexically_normal())
        {
            score += 500;
        }
        if (candidate == (cwd / "backend").lexically_normal())
        {
            score += 30;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestCandidate = candidate;
        }
    }

    return bestCandidate.empty() ? (cwd / "backend") : bestCandidate;
}

std::filesystem::path backendPythonPath()
{
    return backendRootPath() / ".venv" / "Scripts" / "python.exe";
}

std::filesystem::path runtimeLogsDirectory()
{
    return backendRootPath() / "runtime" / "logs";
}

std::filesystem::path runtimeInstallLogPath()
{
    return runtimeLogsDirectory() / "ace_step_runtime_install_last.log";
}

std::string makePendingMusicGenerationTaskId()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "music-generation-pending-" + std::to_string(millis);
}

void appendRuntimeInstallLog(const std::filesystem::path& logPath, const std::string& text)
{
    if (logPath.empty() || text.empty())
    {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(logPath.parent_path(), ec);
    std::ofstream out(logPath, std::ios::app | std::ios::binary);
    if (!out)
    {
        return;
    }

    out << text;
    if (text.back() != '\n')
    {
        out << '\n';
    }
}

void resetRuntimeInstallLog(const std::filesystem::path& logPath)
{
    std::error_code ec;
    std::filesystem::create_directories(logPath.parent_path(), ec);
    std::ofstream out(logPath, std::ios::trunc | std::ios::binary);
    if (!out)
    {
        return;
    }

    const auto now = std::time(nullptr);
    out << "ACE-Step runtime install log\n";
    out << "timestamp: " << static_cast<long long>(now) << "\n";
    out << "workspace: " << std::filesystem::current_path().string() << "\n";
    out << "backend_root: " << backendRootPath().string() << "\n\n";
}

struct CommandRunResult
{
    bool started{false};
    bool timedOut{false};
    int exitCode{-1};
    std::string output;
};

CommandRunResult runCommandCapture(const juce::StringArray& commandLine, int timeoutMs)
{
    CommandRunResult result;
    juce::ChildProcess process;
    result.started = process.start(commandLine);
    if (!result.started)
    {
        return result;
    }

    if (!process.waitForProcessToFinish(timeoutMs))
    {
        result.timedOut = true;
        process.kill();
        result.output = process.readAllProcessOutput().toStdString();
        return result;
    }

    result.exitCode = process.getExitCode();
    result.output = process.readAllProcessOutput().toStdString();
    return result;
}

enum class RuntimeValidationStage
{
    Success,
    ExecutableMissing,
    LaunchFailed,
    ImportFailed,
    HealthcheckFailed,
    UnsupportedLayout
};

std::string runtimeValidationStageLabel(RuntimeValidationStage stage)
{
    switch (stage)
    {
    case RuntimeValidationStage::Success: return "success";
    case RuntimeValidationStage::ExecutableMissing: return "executable_missing";
    case RuntimeValidationStage::LaunchFailed: return "launch_failed";
    case RuntimeValidationStage::ImportFailed: return "import_failed";
    case RuntimeValidationStage::HealthcheckFailed: return "healthcheck_failed";
    case RuntimeValidationStage::UnsupportedLayout: return "unsupported_layout";
    }

    return "unsupported_layout";
}

struct RuntimeLaunchSpec
{
    enum class Kind
    {
        Api,
        Executable,
        PythonScript,
        PythonModule
    };

    Kind kind{Kind::Executable};
    std::filesystem::path path;
    std::string moduleName;
    std::string description;

    juce::StringArray makeCommandLine(const std::vector<std::string>& extraArgs = {}) const
    {
        juce::StringArray commandLine;
        switch (kind)
        {
        case Kind::Api:
            break;
        case Kind::Executable:
            commandLine.add(path.string());
            break;
        case Kind::PythonScript:
            commandLine.add(backendPythonPath().string());
            commandLine.add(path.string());
            break;
        case Kind::PythonModule:
            commandLine.add(backendPythonPath().string());
            commandLine.add("-m");
            commandLine.add(moduleName);
            break;
        }

        for (const auto& arg : extraArgs)
        {
            commandLine.add(arg);
        }
        return commandLine;
    }
};

struct RuntimeValidationReport
{
    RuntimeValidationStage stage{RuntimeValidationStage::UnsupportedLayout};
    std::string summary;
    std::string detail;
    std::string entrypointDescription;
};

std::vector<std::filesystem::path> aceStepScriptCandidates()
{
    const auto scriptsDir = backendRootPath() / ".venv" / "Scripts";
    return {
        scriptsDir / "ace_step.exe",
        scriptsDir / "ace_step.cmd",
        scriptsDir / "ace_step.bat",
        scriptsDir / "ace_step",
        scriptsDir / "acestep.exe",
        scriptsDir / "acestep.cmd",
        scriptsDir / "acestep.bat",
        scriptsDir / "acestep",
        scriptsDir / "acestep-api.exe",
        scriptsDir / "acestep-api.cmd",
        scriptsDir / "acestep_api.exe",
        scriptsDir / "ace_step_api.exe",
        scriptsDir / "ace_step-script.py",
        scriptsDir / "acestep-script.py",
        scriptsDir / "ace_step_api-script.py",
        scriptsDir / "acestep_api-script.py",
    };
}

std::vector<std::string> aceStepModuleCandidates()
{
    std::vector<std::string> modules;
    const auto configured = trimCopy(std::getenv("MOON_ACE_STEP_RUNTIME_MODULE") != nullptr ? std::getenv("MOON_ACE_STEP_RUNTIME_MODULE") : "");
    if (!configured.empty())
    {
        modules.push_back(configured);
    }

    for (const auto* candidate : {
             "ace_step",
             "acestep",
             "ace_step.cli",
             "acestep.cli",
             "ace_step_api",
             "acestep_api"})
    {
        if (std::find(modules.begin(), modules.end(), candidate) == modules.end())
        {
            modules.emplace_back(candidate);
        }
    }

    return modules;
}

bool pythonModuleImportable(const std::string& moduleName)
{
    if (moduleName.empty() || !std::filesystem::exists(backendPythonPath()))
    {
        return false;
    }

    juce::StringArray commandLine;
    commandLine.add(backendPythonPath().string());
    commandLine.add("-c");
    commandLine.add("import importlib.util,sys; sys.exit(0 if importlib.util.find_spec(sys.argv[1]) else 1)");
    commandLine.add(moduleName);

    const auto result = runCommandCapture(commandLine, 15000);
    return result.started && !result.timedOut && result.exitCode == 0;
}

std::optional<RuntimeLaunchSpec> resolveLocalAceStepRuntimeLaunchSpec(std::vector<std::string>* diagnostics = nullptr, bool allowImportProbe = true)
{
    const auto configuredApi = trimCopy(std::getenv("MOON_ACE_STEP_API_URL") != nullptr ? std::getenv("MOON_ACE_STEP_API_URL") : "");
    const auto configuredExecutable = trimCopy(std::getenv("MOON_ACE_STEP_EXECUTABLE") != nullptr ? std::getenv("MOON_ACE_STEP_EXECUTABLE") : "");
    if (!configuredApi.empty())
    {
        if (diagnostics != nullptr)
        {
            diagnostics->push_back("accepted API runtime from MOON_ACE_STEP_API_URL");
        }
        return RuntimeLaunchSpec{RuntimeLaunchSpec::Kind::Api, {}, {}, "configured API runtime"};
    }

    if (!configuredExecutable.empty())
    {
        if (std::filesystem::exists(configuredExecutable))
        {
            if (diagnostics != nullptr)
            {
                diagnostics->push_back("accepted explicit executable from MOON_ACE_STEP_EXECUTABLE: " + configuredExecutable);
            }
            return RuntimeLaunchSpec{RuntimeLaunchSpec::Kind::Executable, configuredExecutable, {}, "explicit executable runtime"};
        }

        if (diagnostics != nullptr)
        {
            diagnostics->push_back("rejected explicit executable from MOON_ACE_STEP_EXECUTABLE because the file does not exist: " + configuredExecutable);
        }
    }

    for (const auto& candidate : aceStepScriptCandidates())
    {
        if (!std::filesystem::exists(candidate))
        {
            continue;
        }

        RuntimeLaunchSpec spec;
        if (candidate.extension() == ".py")
        {
            spec.kind = RuntimeLaunchSpec::Kind::PythonScript;
        }
        else
        {
            spec.kind = RuntimeLaunchSpec::Kind::Executable;
        }
        spec.path = candidate;
        spec.description = "embedded backend/.venv runtime";
        if (diagnostics != nullptr)
        {
            diagnostics->push_back("accepted backend/.venv runtime candidate: " + candidate.string());
        }
        return spec;
    }

    if (diagnostics != nullptr)
    {
        diagnostics->push_back("no known ACE-Step script entrypoints were found in backend/.venv/Scripts");
    }

    if (!allowImportProbe)
    {
        if (diagnostics != nullptr)
        {
            diagnostics->push_back("skipped Python module runtime probe on UI thread");
        }
        return std::nullopt;
    }

    for (const auto& moduleName : aceStepModuleCandidates())
    {
        if (pythonModuleImportable(moduleName))
        {
            if (diagnostics != nullptr)
            {
                diagnostics->push_back("accepted Python module runtime candidate: " + moduleName);
            }
            return RuntimeLaunchSpec{RuntimeLaunchSpec::Kind::PythonModule, {}, moduleName, "python module runtime"};
        }

        if (diagnostics != nullptr)
        {
            diagnostics->push_back("rejected Python module runtime candidate because importlib could not find it: " + moduleName);
        }
    }

    return std::nullopt;
}

bool localAceStepRuntimeAvailable(std::string* detail = nullptr)
{
    std::vector<std::string> diagnostics;
    const auto launchSpec = resolveLocalAceStepRuntimeLaunchSpec(&diagnostics, false);
    if (launchSpec.has_value())
    {
        if (detail != nullptr)
        {
            *detail = "Using " + launchSpec->description;
        }
        return true;
    }

    if (detail != nullptr)
    {
        std::ostringstream message;
        message << "ACE-Step runtime is not installed or not runnable. Moon will prepare it automatically when generation starts.";
        if (!diagnostics.empty())
        {
            message << " Last check: " << diagnostics.back();
        }
        *detail = message.str();
    }
    return false;
}

RuntimeValidationReport validateLocalAceStepRuntime()
{
    RuntimeValidationReport report;

    const auto configuredApi = trimCopy(std::getenv("MOON_ACE_STEP_API_URL") != nullptr ? std::getenv("MOON_ACE_STEP_API_URL") : "");
    if (!configuredApi.empty())
    {
        report.stage = RuntimeValidationStage::Success;
        report.summary = "configured API runtime";
        report.detail = "ACE-Step API runtime is configured via MOON_ACE_STEP_API_URL";
        report.entrypointDescription = configuredApi;
        return report;
    }

    if (!std::filesystem::exists(backendPythonPath()))
    {
        report.stage = RuntimeValidationStage::UnsupportedLayout;
        report.summary = "Embedded backend Python is missing";
        report.detail = "backend/.venv/Scripts/python.exe was not found";
        return report;
    }

    const auto configuredExecutable = trimCopy(std::getenv("MOON_ACE_STEP_EXECUTABLE") != nullptr ? std::getenv("MOON_ACE_STEP_EXECUTABLE") : "");
    if (!configuredExecutable.empty() && !std::filesystem::exists(configuredExecutable))
    {
        report.stage = RuntimeValidationStage::ExecutableMissing;
        report.summary = "Configured ACE-Step executable is missing";
        report.detail = "MOON_ACE_STEP_EXECUTABLE points to a missing file: " + configuredExecutable;
        return report;
    }

    std::vector<std::string> diagnostics;
    const auto launchSpec = resolveLocalAceStepRuntimeLaunchSpec(&diagnostics);
    if (!launchSpec.has_value())
    {
        report.stage = RuntimeValidationStage::ImportFailed;
        report.summary = "ACE-Step runtime could not be imported or discovered";
        std::ostringstream detail;
        detail << "No runnable ACE-Step entrypoint was found.";
        for (const auto& diagnostic : diagnostics)
        {
            detail << "\n- " << diagnostic;
        }
        report.detail = detail.str();
        return report;
    }

    report.entrypointDescription = launchSpec->description;
    if (launchSpec->kind == RuntimeLaunchSpec::Kind::Api)
    {
        report.stage = RuntimeValidationStage::Success;
        report.summary = "ACE-Step API runtime is configured";
        report.detail = "Configured ACE-Step API runtime is available for backend startup checks.";
        return report;
    }

    const auto helpResult = runCommandCapture(launchSpec->makeCommandLine({"--help"}), 15000);
    if (!helpResult.started)
    {
        report.stage = RuntimeValidationStage::LaunchFailed;
        report.summary = "ACE-Step runtime could not be launched";
        report.detail = "The resolved runtime entrypoint could not be started.";
        return report;
    }

    if (helpResult.timedOut)
    {
        report.stage = RuntimeValidationStage::LaunchFailed;
        report.summary = "ACE-Step runtime validation timed out";
        report.detail = helpResult.output.empty() ? "Timed out while running --help" : helpResult.output;
        return report;
    }

    if (helpResult.exitCode != 0)
    {
        const auto altHelp = runCommandCapture(launchSpec->makeCommandLine({"-h"}), 15000);
        if (!altHelp.started || altHelp.timedOut || altHelp.exitCode != 0)
        {
            report.stage = RuntimeValidationStage::LaunchFailed;
            report.summary = "ACE-Step runtime launch failed";
            report.detail = !altHelp.output.empty() ? altHelp.output : helpResult.output;
            if (report.detail.empty())
            {
                report.detail = "The runtime entrypoint exited without a successful help response.";
            }
            return report;
        }
    }

    report.stage = RuntimeValidationStage::Success;
    report.summary = "ACE-Step runtime validated";
    report.detail = "Validated runtime entrypoint: " + launchSpec->description;
    return report;
}

struct RuntimeInstallPlan
{
    std::vector<std::string> command;
    std::string sourceDescription;
    std::vector<std::string> resolutionLog;
    bool localArtifact{false};
};

int runtimeArtifactPriority(const std::filesystem::path& path)
{
    const auto lower = path.filename().string();
    const auto ext = path.extension().string();
    if (ext == ".whl")
    {
        if (lower.find("win_amd64") != std::string::npos || lower.find("any.whl") != std::string::npos)
        {
            return 0;
        }
        return 1;
    }

    if ((lower.size() >= 7 && lower.substr(lower.size() - 7) == ".tar.gz") || ext == ".gz")
    {
        return 2;
    }
    if (ext == ".zip")
    {
        return 3;
    }
    if (ext == ".tar")
    {
        return 4;
    }

    return 100;
}

bool lineContainsAny(const std::string& text, std::initializer_list<const char*> needles)
{
    for (const auto* needle : needles)
    {
        if (text.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

RuntimeInstallPlan resolveAceStepRuntimeInstallPlan()
{
    RuntimeInstallPlan plan;
    const auto pythonPath = backendPythonPath();
    const auto artifactEnv = trimCopy(std::getenv("MOON_ACE_STEP_RUNTIME_ARTIFACT") != nullptr ? std::getenv("MOON_ACE_STEP_RUNTIME_ARTIFACT") : "");
    const auto specEnv = trimCopy(std::getenv("MOON_ACE_STEP_RUNTIME_PIP_SPEC") != nullptr ? std::getenv("MOON_ACE_STEP_RUNTIME_PIP_SPEC") : "");
    const auto gitEnv = trimCopy(std::getenv("MOON_ACE_STEP_RUNTIME_GIT_SOURCE") != nullptr ? std::getenv("MOON_ACE_STEP_RUNTIME_GIT_SOURCE") : "");

    {
        std::vector<std::string> runtimeDiagnostics;
        const auto existingRuntime = resolveLocalAceStepRuntimeLaunchSpec(&runtimeDiagnostics);
        if (existingRuntime.has_value() && existingRuntime->kind != RuntimeLaunchSpec::Kind::Api)
        {
            plan.resolutionLog.push_back("rejected install source selection because a runnable ACE-Step runtime is already present: " + existingRuntime->description);
        }
        else if (!runtimeDiagnostics.empty())
        {
            for (const auto& message : runtimeDiagnostics)
            {
                plan.resolutionLog.push_back(message);
            }
        }
    }

    plan.command = {
        pythonPath.string(),
        "-m",
        "pip",
        "install",
        "--disable-pip-version-check",
        "--no-input",
        "--upgrade",
        "--prefer-binary",
    };

    if (!artifactEnv.empty())
    {
        if (std::filesystem::exists(artifactEnv))
        {
            plan.sourceDescription = "explicit local artifact: " + artifactEnv;
            plan.command.push_back("--no-index");
            plan.command.push_back(artifactEnv);
            plan.localArtifact = true;
            return plan;
        }

        plan.resolutionLog.push_back("rejected explicit local artifact from MOON_ACE_STEP_RUNTIME_ARTIFACT because it does not exist: " + artifactEnv);
    }

    const auto runtimeDirectory = backendRootPath() / "runtime";
    if (std::filesystem::exists(runtimeDirectory))
    {
        std::vector<std::filesystem::path> localArtifacts;
        for (const auto& entry : std::filesystem::directory_iterator(runtimeDirectory))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto priority = runtimeArtifactPriority(entry.path());
            if (priority < 100)
            {
                localArtifacts.push_back(entry.path());
            }
        }

        std::sort(localArtifacts.begin(), localArtifacts.end(), [](const auto& lhs, const auto& rhs)
        {
            const auto leftPriority = runtimeArtifactPriority(lhs);
            const auto rightPriority = runtimeArtifactPriority(rhs);
            if (leftPriority != rightPriority)
            {
                return leftPriority < rightPriority;
            }
            return lhs.filename().string() < rhs.filename().string();
        });

        if (!localArtifacts.empty())
        {
            plan.sourceDescription = "bundled local artifact: " + localArtifacts.front().string();
            plan.command.push_back("--no-index");
            plan.command.push_back(localArtifacts.front().string());
            plan.localArtifact = true;
            return plan;
        }

        plan.resolutionLog.push_back("rejected bundled local artifact source because backend/runtime/ does not contain a supported wheel or source archive");
    }
    else
    {
        plan.resolutionLog.push_back("rejected bundled local artifact source because backend/runtime/ is missing");
    }

    if (!specEnv.empty())
    {
        plan.sourceDescription = "explicit pip spec: " + specEnv;
        plan.command.push_back(specEnv);
        return plan;
    }
    plan.resolutionLog.push_back("rejected explicit pip spec because MOON_ACE_STEP_RUNTIME_PIP_SPEC is empty");

    if (!gitEnv.empty())
    {
        plan.sourceDescription = "explicit git source: " + gitEnv;
        plan.command.push_back(gitEnv);
        return plan;
    }
    plan.resolutionLog.push_back("rejected explicit git source because MOON_ACE_STEP_RUNTIME_GIT_SOURCE is empty");

    plan.sourceDescription = "default upstream fallback: git+https://github.com/ace-step/ACE-Step.git";
    plan.command.push_back("git+https://github.com/ace-step/ACE-Step.git");
    plan.resolutionLog.push_back("selected final default upstream fallback");
    return plan;
}
}

struct AppController::RuntimePrepareState
{
    enum class Phase
    {
        Idle,
        Checking,
        ResolvingSource,
        PreparingEnv,
        Downloading,
        Installing,
        Validating,
        RestartingBackend,
        Completed,
        Failed,
        Cancelled
    };

    struct Result
    {
        bool success{false};
        bool cancelled{false};
        std::string errorMessage;
    };

    mutable std::mutex mutex;
    Phase phase{Phase::Idle};
    double progress{0.0};
    std::string message;
    std::string sourceDescription;
    std::string lastError;
    std::string recentOutput;
    std::filesystem::path logPath;
    bool active{false};
    bool cancelRequested{false};
    std::shared_ptr<juce::ChildProcess> process;
    std::future<Result> future;

    void setPhase(Phase newPhase, double newProgress, std::string newMessage)
    {
        std::lock_guard<std::mutex> lock(mutex);
        phase = newPhase;
        progress = newProgress;
        message = std::move(newMessage);
    }

    void appendOutput(const std::string& chunk)
    {
        if (chunk.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);
        recentOutput += chunk;
        constexpr std::size_t kMaxLogSize = 4000;
        if (recentOutput.size() > kMaxLogSize)
        {
            recentOutput.erase(0, recentOutput.size() - kMaxLogSize);
        }
    }

    bool cancelled() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return cancelRequested;
    }
};

struct AppController::PreviewRenderState
{
    struct Result
    {
        bool success{false};
        std::uint64_t generation{0};
        std::filesystem::path outputPath;
        double durationSec{0.0};
        std::string errorMessage;
    };

    mutable std::mutex mutex;
    bool active{false};
    std::uint64_t generation{0};
    std::filesystem::path outputPath;
    double durationSec{0.0};
    std::string lastError;
    std::future<Result> future;
};

AppController::AppController()
{
    appendBootstrapTrace("[bootstrap] AppController ctor begin");
    logger_ = std::make_unique<moon::engine::Logger>();
    appendBootstrapTrace("[bootstrap] logger created");
    settingsService_ = std::make_unique<moon::engine::SettingsService>();
    appendBootstrapTrace("[bootstrap] settings service created");
    settings_ = settingsService_->load();
    appendBootstrapTrace("[bootstrap] settings loaded");
    projectManager_ = std::make_unique<moon::engine::ProjectManager>(*logger_);
    appendBootstrapTrace("[bootstrap] project manager created");
    runtimeCoordinator_ = std::make_unique<moon::engine::EngineRuntimeCoordinator>(*logger_);
    appendBootstrapTrace("[bootstrap] runtime coordinator created");
    timeline_ = std::make_unique<moon::engine::TimelineFacade>(*logger_);
    appendBootstrapTrace("[bootstrap] timeline facade created");
    clipOperations_ = std::make_unique<moon::engine::ClipOperations>(*logger_);
    appendBootstrapTrace("[bootstrap] clip operations created");
    transport_ = std::make_unique<moon::engine::TransportFacade>(*logger_);
    appendBootstrapTrace("[bootstrap] transport facade created");
    waveformService_ = std::make_unique<moon::engine::WaveformService>(*logger_);
    appendBootstrapTrace("[bootstrap] waveform service created");
    exportService_ = std::make_unique<moon::engine::ExportService>(*logger_);
    appendBootstrapTrace("[bootstrap] export service created");
    aiJobClient_ = std::make_unique<moon::engine::AIJobClient>(
        settings_.backendUrl.empty() ? std::string(AppConfig::backendUrl) : settings_.backendUrl,
        *logger_);
    appendBootstrapTrace("[bootstrap] ai job client created");
    backendProcessManager_ = std::make_unique<BackendProcessManager>(
        settings_.backendUrl.empty() ? std::string(AppConfig::backendUrl) : settings_.backendUrl,
        *logger_);
    appendBootstrapTrace("[bootstrap] backend process manager created");
    runtimePrepareState_ = std::make_unique<RuntimePrepareState>();
    appendBootstrapTrace("[bootstrap] runtime prepare state created");
    previewRenderState_ = std::make_unique<PreviewRenderState>();
    appendBootstrapTrace("[bootstrap] preview render state created");
    modelManager_ = std::make_unique<moon::engine::ModelManager>(*logger_, modelRootPath());
    appendBootstrapTrace("[bootstrap] model manager created");
    taskManager_ = std::make_unique<moon::engine::TaskManager>(*aiJobClient_, *logger_);
    appendBootstrapTrace("[bootstrap] task manager created");

    transport_->setProjectState(&projectManager_->state());
#if MOON_HAS_TRACKTION
    timeline_->setPreferredBackend(moon::engine::TimelineBackendMode::TracktionHybrid);
    transport_->setPreferredBackend(moon::engine::TransportBackendMode::TracktionHybrid);
#endif
    syncEngineIntegrationState();
    appendBootstrapTrace("[bootstrap] AppController ctor end");
}

AppController::~AppController() = default;

bool AppController::startup()
{
    appendBootstrapTrace("[bootstrap] startup begin");
    logger_->info("AppController startup");

    // Fast-start path: do not block the UI on backend probing.
    backendFallbackNoticeActive_ = false;
    autosaveRecoveryNoticeActive_ = false;
    refreshStartupNoticeState();

    if (!projectManager_->projectFilePath().has_value())
    {
        const auto defaultRoot = (std::filesystem::current_path() / "workspace_project").string();
        createProject("Untitled Project", defaultRoot);
        logger_->info("Created default startup project at " + defaultRoot);
    }

    appendBootstrapTrace("[bootstrap] startup refreshModelRegistry");
    refreshModelRegistry();
    appendBootstrapTrace("[bootstrap] startup refreshBackendStatus");
    refreshBackendStatus();
    logger_->info("Startup completed without blocking backend probe");
    appendBootstrapTrace("[bootstrap] startup end");
    return true;
}

bool AppController::createProject(const std::string& name, const std::string& rootPath)
{
    const auto created = projectManager_->createProject(name, rootPath);
    if (created)
    {
        projectManager_->state().sampleRate = settings_.defaultSampleRate;
        syncEngineIntegrationState();
        previewPlaybackActive_ = false;
        markPreviewPlaybackDirty();
        clearProjectDirty();
        saveProject();
    }
    return created;
}

bool AppController::openProject(const std::string& projectFilePath)
{
    const auto opened = projectManager_->openProject(projectFilePath);
    if (!opened)
    {
        return false;
    }

    auto& state = projectManager_->state();
    for (const auto& [_, asset] : state.sourceAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    for (const auto& [_, asset] : state.generatedAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    previewPlaybackActive_ = false;
    markPreviewPlaybackDirty();
    syncEngineIntegrationState();
    clearProjectDirty();
    syncTransportToSelection();
    return true;
}

bool AppController::restoreAutosave(const std::string& projectRootPath)
{
    const auto restored = projectManager_->restoreFromAutosave(projectRootPath);
    if (!restored)
    {
        return false;
    }

    auto& state = projectManager_->state();
    for (const auto& [_, asset] : state.sourceAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    for (const auto& [_, asset] : state.generatedAssets)
    {
        if (!asset.path.empty())
        {
            waveformService_->markRequested(asset.path);
        }
    }

    previewPlaybackActive_ = false;
    markPreviewPlaybackDirty();
    syncEngineIntegrationState();
    clearProjectDirty();
    syncTransportToSelection();
    return true;
}

bool AppController::saveProject()
{
    const auto saved = projectManager_->saveProject();
    if (saved)
    {
        clearProjectDirty();
    }
    return saved;
}

bool AppController::importAudio(const std::string& audioPath)
{
    auto& state = projectManager_->state();
    if (state.tracks.empty())
    {
        timeline_->ensureTrack(state, "Track 1");
    }

    return importAudioToTrack(audioPath, state.tracks.front().id, 0.0);
}

bool AppController::importAudioToTrack(const std::string& audioPath, const std::string& trackId, double startSec)
{
    if (!std::filesystem::exists(audioPath))
    {
        logger_->error("Import failed, file not found: " + audioPath);
        return false;
    }

    auto& state = projectManager_->state();
    const bool wasEmptyProject = state.clips.empty();
    waveformService_->requestWaveform(audioPath);
    const auto waveformSnapshot = waveformService_->snapshotFor(audioPath);

    int detectedSampleRate = 0;
    double detectedDurationSec = 0.0;
#if MOON_HAS_JUCE
    const auto importMetadata = readAudioImportMetadata(audioPath);
    detectedSampleRate = importMetadata.sampleRate;
    detectedDurationSec = importMetadata.durationSec;
#endif

    if (detectedSampleRate <= 0 && waveformSnapshot.data != nullptr && waveformSnapshot.data->sampleRate > 0)
    {
        detectedSampleRate = waveformSnapshot.data->sampleRate;
    }

    if (detectedDurationSec <= 0.0 && waveformSnapshot.data != nullptr)
    {
        detectedDurationSec = waveformSnapshot.data->durationSec;
    }

    if (wasEmptyProject && detectedSampleRate > 0)
    {
        state.sampleRate = detectedSampleRate;
        logger_->info("Adjusted project sample rate to imported WAV sample rate: " + std::to_string(state.sampleRate));
    }

    const auto durationSec = std::max(0.1, detectedDurationSec);
    const auto resolvedTrackId = !trackId.empty() ? trackId : timeline_->ensureTrack(state, "Track 1");
    const auto clipId = timeline_->insertAudioClip(state, resolvedTrackId, audioPath, std::max(0.0, startSec), durationSec);
    timeline_->selectClip(state, clipId);
    state.uiState.selectedTrackId = resolvedTrackId;
    state.uiState.playheadSec = std::max(0.0, startSec);
    markPreviewPlaybackDirty();
    markProjectDirty();
    logger_->info("Imported audio clip into project playback path: " + clipId);
    return true;
}

std::optional<std::string> AppController::projectFilePath() const
{
    return projectManager_->projectFilePath();
}

std::string AppController::backendStatusSummary() const
{
    std::string summary = backendHealth_.backend == "local-job-service" ? "Local jobs" : (aiJobClient_->backendReachable() ? "Backend live" : "Backend fallback");
    const auto route = transport_->playbackRouteSummary();
    if (route == "project-live")
    {
        summary += " | live";
    }
    else if (route == "project-cached-preview")
    {
        summary += previewPlaybackDirty_ ? " | stale preview" : " | cached preview";
    }
    else
    {
        summary += previewPlaybackDirty_ ? " | stale" : " | " + route;
    }

    const auto diagnostic = transport_->projectPlaybackDiagnostic();
    if (!diagnostic.empty() && diagnostic != "ok")
    {
        summary += " | " + diagnostic;
    }

    return summary;
}

bool AppController::separateStemsForSelectedClip()
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr)
    {
        logger_->error("Separate stems requested with no selected clip");
        return false;
    }

    const auto assetPath = resolveAssetPath(state, *clip);
    if (assetPath.empty())
    {
        logger_->error("Selected clip has no asset path");
        return false;
    }

    taskManager_->queueStems(clip->id, assetPath, clip->startSec);
    markProjectDirty();
    return true;
}

bool AppController::rewriteSelectedRegion(const std::string& prompt)
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr || !state.uiState.hasSelectedRegion)
    {
        logger_->error("Rewrite requested without selected clip/region");
        return false;
    }

    const auto duration = std::max(0.1, state.uiState.selectedRegionEndSec - state.uiState.selectedRegionStartSec);
    const auto tempPath = projectManager_->cacheDirectory() / (clip->id + "_rewrite_region.wav");
    if (!exportService_->exportRegion(state, state.uiState.selectedRegionStartSec, state.uiState.selectedRegionEndSec, tempPath))
    {
        logger_->error("Failed to export temp region for rewrite");
        return false;
    }

    taskManager_->queueRewrite(clip->id, tempPath.string(), state.uiState.selectedRegionStartSec, duration, prompt);
    markProjectDirty();
    return true;
}

bool AppController::addGeneratedLayer(const std::string& prompt)
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr || !state.uiState.hasSelectedRegion)
    {
        logger_->error("Add layer requested without selected clip/region");
        return false;
    }

    const auto duration = std::max(0.1, state.uiState.selectedRegionEndSec - state.uiState.selectedRegionStartSec);
    const auto tempPath = projectManager_->cacheDirectory() / (clip->id + "_add_layer_region.wav");
    if (!exportService_->exportRegion(state, state.uiState.selectedRegionStartSec, state.uiState.selectedRegionEndSec, tempPath))
    {
        logger_->error("Failed to export temp region for add-layer");
        return false;
    }

    taskManager_->queueAddLayer(clip->id, tempPath.string(), state.uiState.selectedRegionStartSec, duration, prompt);
    markProjectDirty();
    return true;
}

std::optional<std::string> AppController::generateMusic(const moon::engine::MusicGenerationRequest& request)
{
    lastMusicGenerationError_.clear();
    const auto failGeneration = [this](const std::string& message) -> std::optional<std::string>
    {
        lastMusicGenerationError_ = message;
        logger_->error(message);
        return std::nullopt;
    };

    const auto stylesPrompt = trimCopy(request.stylesPrompt);
    const auto secondaryPrompt = trimCopy(request.secondaryPrompt.empty() ? request.lyricsPrompt : request.secondaryPrompt);
    if (stylesPrompt.empty() && secondaryPrompt.empty())
    {
        return failGeneration("Enter style or notes before generating.");
    }

    auto queuedRequest = request;
    queuedRequest.stylesPrompt = stylesPrompt;
    queuedRequest.secondaryPrompt = secondaryPrompt;
    queuedRequest.secondaryPromptIsLyrics = moon::engine::generationTargetProfile(queuedRequest.category).secondaryPromptRepresentsLyrics;
    queuedRequest.lyricsPrompt = queuedRequest.secondaryPromptIsLyrics ? secondaryPrompt : std::string{};
    queuedRequest.isInstrumental = !queuedRequest.secondaryPromptIsLyrics || secondaryPrompt.empty();

    if (modelManager_ == nullptr)
    {
        return failGeneration("Model manager is unavailable.");
    }

    std::string refreshError;
    if (modelManager_->refresh(&refreshError))
    {
        modelRegistrySnapshot_ = modelManager_->snapshot();
        applyRuntimeReadinessOverlay();
    }
    else if (!refreshError.empty())
    {
        logger_->warning("Model registry refresh before generation failed: " + refreshError);
    }

    const auto capability = moon::engine::generationTargetCapability(queuedRequest.category);
    const auto statusCanEnterGenerationPipeline = [](moon::engine::ModelStatus status)
    {
        switch (status)
        {
        case moon::engine::ModelStatus::Downloaded:
        case moon::engine::ModelStatus::RuntimeMissing:
        case moon::engine::ModelStatus::RuntimePreparing:
        case moon::engine::ModelStatus::Ready:
        case moon::engine::ModelStatus::Running:
        case moon::engine::ModelStatus::Failed:
        case moon::engine::ModelStatus::UpdateAvailable:
            return true;
        case moon::engine::ModelStatus::NotInstalled:
        case moon::engine::ModelStatus::Downloading:
        case moon::engine::ModelStatus::Verifying:
        case moon::engine::ModelStatus::Broken:
        case moon::engine::ModelStatus::Incompatible:
        case moon::engine::ModelStatus::Removing:
            return false;
        }

        return false;
    };
    const auto snapshotCandidateById = [&](const std::string& modelId) -> std::optional<moon::engine::ResolvedModelInfo>
    {
        for (const auto& installed : modelRegistrySnapshot_.installed)
        {
            if (installed.id != modelId || !statusCanEnterGenerationPipeline(installed.status))
            {
                continue;
            }

            if (std::find(installed.capabilities.begin(), installed.capabilities.end(), capability) == installed.capabilities.end())
            {
                continue;
            }

            return moon::engine::ResolvedModelInfo{
                installed.id,
                installed.displayName,
                installed.version,
                installed.installPath,
                installed.capabilities,
                installed.status};
        }

        return std::nullopt;
    };
    const auto firstSnapshotCandidate = [&]() -> std::optional<moon::engine::ResolvedModelInfo>
    {
        for (const auto& installed : modelRegistrySnapshot_.installed)
        {
            if (!statusCanEnterGenerationPipeline(installed.status))
            {
                continue;
            }

            if (std::find(installed.capabilities.begin(), installed.capabilities.end(), capability) == installed.capabilities.end())
            {
                continue;
            }

            return moon::engine::ResolvedModelInfo{
                installed.id,
                installed.displayName,
                installed.version,
                installed.installPath,
                installed.capabilities,
                installed.status};
        }

        return std::nullopt;
    };

    std::optional<moon::engine::ResolvedModelInfo> resolvedModel;
    if (!queuedRequest.selectedModel.empty())
    {
        resolvedModel = modelManager_->resolveInstalledModel(queuedRequest.selectedModel);
        if (!resolvedModel.has_value())
        {
            resolvedModel = snapshotCandidateById(queuedRequest.selectedModel);
        }
    }
    else
    {
        resolvedModel = modelManager_->resolveActiveModel(capability);
        if (!resolvedModel.has_value())
        {
            const auto activeBinding = modelRegistrySnapshot_.activeBindings.find(capability);
            if (activeBinding != modelRegistrySnapshot_.activeBindings.end())
            {
                resolvedModel = snapshotCandidateById(activeBinding->second);
            }
        }
        if (!resolvedModel.has_value())
        {
            resolvedModel = firstSnapshotCandidate();
        }
    }

    if (!resolvedModel.has_value())
    {
        return failGeneration("No installed model is selected for this generation target.");
    }

    queuedRequest.selectedModel = resolvedModel->id;
    queuedRequest.selectedModelDisplayName = resolvedModel->displayName;
    queuedRequest.selectedModelVersion = resolvedModel->version;
    queuedRequest.selectedModelPath = resolvedModel->installPath.string();

    const auto queuePendingGeneration = [this, &queuedRequest](const std::string& initialStatus, double initialProgress) -> std::optional<std::string>
    {
        if (pendingMusicGenerationTaskId_.empty())
        {
            pendingMusicGenerationTaskId_ = makePendingMusicGenerationTaskId();
        }
        pendingMusicGenerationRequest_ = queuedRequest;
        pendingMusicGenerationRealJobId_.clear();
        taskManager_->upsertTask(moon::engine::TaskInfo{
            pendingMusicGenerationTaskId_,
            "music-generation",
            "running",
            initialProgress,
            initialStatus.empty() ? std::string("Preparing local AI runtime") : initialStatus});

        std::string prepareError;
        if (!prepareGenerationRuntime(&prepareError, true) && prepareError != "Runtime preparation is already running")
        {
            const auto message = prepareError.empty()
                ? std::string("Could not start local AI runtime preparation")
                : prepareError;
            const auto failedTaskId = pendingMusicGenerationTaskId_;
            taskManager_->upsertTask(moon::engine::TaskInfo{
                failedTaskId,
                "music-generation",
                "failed",
                1.0,
                message});
            pendingMusicGenerationRequest_.reset();
            pendingMusicGenerationRealJobId_.clear();
            pendingMusicGenerationTaskId_.clear();
            return failedTaskId;
        }

        modelRegistrySnapshot_ = modelManager_->snapshot();
        applyRuntimeReadinessOverlay();
        lastMusicGenerationError_.clear();
        return pendingMusicGenerationTaskId_;
    };

    std::string runtimeDetail;
    const bool runtimeAvailable = localAceStepRuntimeAvailable(&runtimeDetail);
    const bool backendReachable = backendProcessManager_ != nullptr && backendProcessManager_->probeBackendReady();
    if (!runtimeAvailable && !backendReachable)
    {
        return queuePendingGeneration("Preparing local AI runtime", 0.02);
    }

    if (!backendReachable)
    {
        return queuePendingGeneration("Starting local AI backend", 0.18);
    }

    if (aiJobClient_ != nullptr && backendProcessManager_ != nullptr)
    {
        aiJobClient_->setBackendUrl(backendProcessManager_->backendUrl());
    }
    return queueResolvedMusicGeneration(std::move(queuedRequest));
}

std::optional<std::string> AppController::queueResolvedMusicGeneration(moon::engine::MusicGenerationRequest queuedRequest)
{
    auto& state = projectManager_->state();
    if (queuedRequest.bpm <= 0.0)
    {
        queuedRequest.bpm = state.tempo;
    }
    const auto startSec = std::max(0.0, state.uiState.playheadSec);
    std::string targetTrackId;
    if (!state.uiState.selectedTrackId.empty())
    {
        const bool selectedTrackHasClip = std::any_of(
            state.clips.begin(),
            state.clips.end(),
            [&state](const moon::engine::ClipInfo& clip)
            {
                return clip.trackId == state.uiState.selectedTrackId;
            });
        if (!selectedTrackHasClip)
        {
            targetTrackId = state.uiState.selectedTrackId;
        }
    }

    const auto preferredTrackName = std::string(moon::engine::generationTargetProfile(queuedRequest.category).suggestedTrackName);
    const auto jobId = taskManager_->queueMusicGeneration(queuedRequest, targetTrackId, preferredTrackName, startSec);
    markProjectDirty();
    return jobId;
}

bool AppController::exportFullMix(const std::string& outputPath)
{
    return exportService_->exportMix(projectManager_->state(), outputPath);
}

bool AppController::exportSelectedRegion(const std::string& outputPath)
{
    const auto& state = projectManager_->state();
    if (!state.uiState.hasSelectedRegion)
    {
        logger_->error("Export selected region requested with no selected region");
        return false;
    }

    return exportService_->exportRegion(
        state,
        state.uiState.selectedRegionStartSec,
        state.uiState.selectedRegionEndSec,
        outputPath);
}

bool AppController::exportStemTracks(const std::string& outputDirectory)
{
    return exportService_->exportStemTracks(projectManager_->state(), outputDirectory);
}

bool AppController::duplicateSelectedClip()
{
    return finalizeTimelineEdit(
        [this]()
        {
            return clipOperations_->duplicateSelected(projectManager_->state());
        },
        false,
        {},
        "duplicateSelectedClip");
}

bool AppController::deleteSelectedClip()
{
    return finalizeTimelineEdit(
        [this]()
        {
            return clipOperations_->deleteSelected(projectManager_->state());
        },
        true,
        {},
        "deleteSelectedClip");
}

bool AppController::splitSelectedClipAtPlayhead()
{
    auto& state = projectManager_->state();
    const auto* clip = findSelectedClip(state);
    if (clip == nullptr)
    {
        logger_->warning("Split clip skipped because no clip is selected");
        return false;
    }

    const auto timelineSplitSec = currentTimelinePlayheadSec();
    return finalizeTimelineEdit(
        [this, &state, timelineSplitSec]()
        {
            return clipOperations_->splitSelected(state, timelineSplitSec);
        },
        true,
        "Split clip skipped because playhead is outside the selected clip body",
        "splitSelectedClip");
}

bool AppController::setSelectedClipGain(double gain)
{
    return finalizeTimelineEdit(
        [this, gain]()
        {
            return clipOperations_->setSelectedGain(projectManager_->state(), gain);
        },
        false,
        {},
        "setSelectedClipGain");
}

bool AppController::setSelectedClipFadeIn(double fadeSec)
{
    return finalizeTimelineEdit(
        [this, fadeSec]()
        {
            return clipOperations_->setSelectedFadeIn(projectManager_->state(), fadeSec);
        },
        false,
        {},
        "setSelectedClipFadeIn");
}

bool AppController::setSelectedClipFadeOut(double fadeSec)
{
    return finalizeTimelineEdit(
        [this, fadeSec]()
        {
            return clipOperations_->setSelectedFadeOut(projectManager_->state(), fadeSec);
        },
        false,
        {},
        "setSelectedClipFadeOut");
}

bool AppController::activateSelectedTake()
{
    return finalizeTimelineEdit(
        [this]()
        {
            return clipOperations_->activateSelectedTake(projectManager_->state());
        },
        false,
        "Activate take skipped because selected clip is not part of a take group",
        "activateSelectedTake");
}

bool AppController::trimSelectedClipLeft(double deltaSec)
{
    return finalizeTimelineEdit(
        [this, deltaSec]()
        {
            return clipOperations_->trimSelectedLeft(projectManager_->state(), deltaSec);
        },
        true,
        {},
        "trimSelectedClipLeft");
}

bool AppController::trimSelectedClipRight(double deltaSec)
{
    return finalizeTimelineEdit(
        [this, deltaSec]()
        {
            return clipOperations_->trimSelectedRight(projectManager_->state(), deltaSec);
        },
        true,
        {},
        "trimSelectedClipRight");
}

bool AppController::createCrossfadeWithPrevious(double overlapSec)
{
    return finalizeTimelineEdit(
        [this, overlapSec]()
        {
            return clipOperations_->createCrossfadeWithPrevious(projectManager_->state(), overlapSec);
        },
        true,
        "Create previous crossfade skipped because no suitable previous clip was found",
        "createCrossfadeWithPrevious");
}

bool AppController::createCrossfadeWithNext(double overlapSec)
{
    return finalizeTimelineEdit(
        [this, overlapSec]()
        {
            return clipOperations_->createCrossfadeWithNext(projectManager_->state(), overlapSec);
        },
        true,
        "Create next crossfade skipped because no suitable next clip was found",
        "createCrossfadeWithNext");
}

bool AppController::moveClipOnTimeline(const std::string& clipId, double newStartSec)
{
    return finalizeTimelineEdit(
        [this, &clipId, newStartSec]()
        {
            return timeline_->moveClip(projectManager_->state(), clipId, newStartSec);
        },
        true,
        {},
        "moveClipOnTimeline");
}

bool AppController::moveClipToTrack(const std::string& clipId, const std::string& trackId, double newStartSec)
{
    return finalizeTimelineEdit(
        [this, &clipId, &trackId, newStartSec]()
        {
            return timeline_->moveClipToTrack(projectManager_->state(), clipId, trackId, newStartSec);
        },
        true,
        {},
        "moveClipToTrack");
}

void AppController::beginInteractiveTimelineEdit()
{
    if (interactiveTimelineEditActive_)
    {
        return;
    }

    interactiveTimelineEditActive_ = true;
    interactiveTimelineEditWasPlaying_ = transport_->isPlaying();
    interactiveTimelineEditPlayheadSec_ = currentTimelinePlayheadSec();
    if (interactiveTimelineEditWasPlaying_ && !shouldUseProjectPreview())
    {
        transport_->pause();
        previewPlaybackActive_ = false;
        logger_->info("Paused transport for interactive timeline edit");
    }
}

void AppController::finishInteractiveTimelineEdit(bool changed, bool syncTransportToSelectionAfterEdit)
{
    if (!interactiveTimelineEditActive_)
    {
        return;
    }

    interactiveTimelineEditActive_ = false;
    if (changed)
    {
        markPreviewPlaybackDirty();
        markProjectDirty();
        if (syncTransportToSelectionAfterEdit && !interactiveTimelineEditWasPlaying_)
        {
            syncTransportToSelection();
        }
        saveProject();
    }

    if (interactiveTimelineEditWasPlaying_ || changed)
    {
        restorePlaybackAfterTimelineEdit(interactiveTimelineEditWasPlaying_, interactiveTimelineEditPlayheadSec_);
    }
    interactiveTimelineEditWasPlaying_ = false;
    interactiveTimelineEditPlayheadSec_ = 0.0;
}

bool AppController::toggleTrackMute(const std::string& trackId)
{
    return finalizeTimelineEdit(
        [this, &trackId]()
        {
            return timeline_->toggleTrackMute(projectManager_->state(), trackId);
        },
        false,
        {},
        "toggleTrackMute");
}

bool AppController::toggleTrackSolo(const std::string& trackId)
{
    return finalizeTimelineEdit(
        [this, &trackId]()
        {
            return timeline_->toggleTrackSolo(projectManager_->state(), trackId);
        },
        false,
        {},
        "toggleTrackSolo");
}

bool AppController::renameTrack(const std::string& trackId, const std::string& newName)
{
    auto sanitizedName = newName;
    sanitizedName.erase(sanitizedName.begin(), std::find_if(sanitizedName.begin(), sanitizedName.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    sanitizedName.erase(std::find_if(sanitizedName.rbegin(), sanitizedName.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), sanitizedName.end());
    if (sanitizedName.empty())
    {
        for (std::size_t index = 0; index < projectManager_->state().tracks.size(); ++index)
        {
            if (projectManager_->state().tracks[index].id == trackId)
            {
                sanitizedName = "Track " + std::to_string(index + 1);
                break;
            }
        }
    }

    return finalizeTimelineEdit(
        [this, &trackId, &sanitizedName]()
        {
            return timeline_->renameTrack(projectManager_->state(), trackId, sanitizedName);
        },
        false,
        {},
        "renameTrack");
}

bool AppController::deleteTrack(const std::string& trackId)
{
    return finalizeTimelineEdit(
        [this, &trackId]()
        {
            return timeline_->deleteTrack(projectManager_->state(), trackId);
        },
        true,
        {},
        "deleteTrack");
}

bool AppController::setTrackColor(const std::string& trackId, const std::string& colorHex)
{
    return finalizeTimelineEdit(
        [this, &trackId, &colorHex]()
        {
            return timeline_->setTrackColor(projectManager_->state(), trackId, colorHex);
        },
        false,
        {},
        "setTrackColor");
}

void AppController::playTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "play");
    if (shouldUseProjectPreview() && ensureProjectPlaybackRoute())
    {
        auto& state = projectManager_->state();
        transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
        transport_->play();
        previewPlaybackActive_ = !transport_->usingProjectPlayback();
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState(transport_->usingProjectPlayback() ? "live project playback active" : "cached project playback active");
        logger_->info(transport_->usingProjectPlayback() ? "Started live project playback" : "Started cached project playback");
        return;
    }

    previewPlaybackActive_ = false;
    syncTransportToSelection();
    if (!transport_->hasLoadedSource() && !transport_->usingProjectPlayback())
    {
        logger_->warning("Transport play skipped because no valid selected source is available");
        syncEngineIntegrationState("transport play skipped; no valid source is available");
        return;
    }
    transport_->play();
    syncEngineIntegrationState("selected-source playback active");
}

void AppController::pauseTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "pause");
    transport_->pause();
    projectManager_->state().uiState.playheadSec = transport_->playheadSec();
    syncEngineIntegrationState("transport paused");
}

void AppController::stopTransport()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "stop");
    transport_->stop();
    auto& state = projectManager_->state();
    previewPlaybackActive_ = false;
    state.uiState.playheadSec = 0.0;
    if (shouldUseProjectPreview() && ensureProjectPlaybackRoute())
    {
        transport_->seek(0.0);
        syncEngineIntegrationState("transport stopped on project playback");
        return;
    }

    syncTransportToSelection();
    syncEngineIntegrationState("transport stopped");
}

moon::engine::Settings AppController::currentSettings() const
{
    return settings_;
}

bool AppController::saveSettings(const moon::engine::Settings& settings)
{
    settings_ = settings;
    const auto saved = settingsService_->save(settings_);
    if (saved)
    {
        logger_->info("Saved app settings to " + settingsService_->settingsPath().string());
        logger_->info("Restart the app to fully apply backend URL changes.");
    }
    else
    {
        logger_->error("Failed to save app settings");
    }
    return saved;
}

bool AppController::pollTasks()
{
    auto& state = projectManager_->state();
    const auto clipCountBefore = state.clips.size();
    const auto generatedCountBefore = state.generatedAssets.size();
    const auto selectedClipBefore = state.uiState.selectedClipId;

    const bool taskChanged = taskManager_->poll(state, *timeline_);
    const bool pendingGenerationChanged = pollPendingMusicGeneration();
    bool stateChanged = false;

    if (state.clips.size() != clipCountBefore || state.generatedAssets.size() != generatedCountBefore)
    {
        markPreviewPlaybackDirty();
        markProjectDirty();
        saveProject();
        stateChanged = true;
        if (state.uiState.selectedClipId != selectedClipBefore)
        {
            syncTransportToSelection();
        }
    }

    return taskChanged || pendingGenerationChanged || stateChanged;
}

void AppController::autosaveIfNeeded()
{
    projectManager_->autosaveProject();
}

void AppController::syncTransportToSelection()
{
    auto& state = projectManager_->state();
    if (previewPlaybackActive_)
    {
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState("preview playback state synced");
        return;
    }

    if (shouldUseProjectPreview())
    {
        if (ensureProjectPlaybackRoute())
        {
            runtimeCoordinator_->noteTransportOperation(
                *projectManager_,
                transport_->usingProjectPlayback() ? "sync-live-project" : "sync-cached-preview");
            const auto timelineSec = std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec());
            if (std::abs(transport_->playheadSec() - timelineSec) > 0.05)
            {
                transport_->seek(timelineSec);
            }
            state.uiState.playheadSec = transport_->playheadSec();
            syncEngineIntegrationState(transport_->usingProjectPlayback() ? "sync via live project playback" : "sync via cached project playback");
            return;
        }
    }

    if (state.uiState.selectedClipId.empty())
    {
        previewPlaybackActive_ = false;
        transport_->clearLoadedSource();
        state.uiState.playheadSec = transport_->playheadSec();
        syncEngineIntegrationState("sync with no selected clip; transport cleared");
        return;
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id != state.uiState.selectedClipId)
        {
            continue;
        }

        const auto assetPath = resolveAssetPath(state, clip);
        if (!assetPath.empty())
        {
            runtimeCoordinator_->noteTransportOperation(*projectManager_, "sync-selected-source");
            if (transport_->sourcePath() != assetPath || std::abs(transport_->sourceDurationSec() - clip.durationSec) > 0.0001)
            {
                transport_->loadSource(assetPath, clip.durationSec);
            }
            if (transport_->hasLoadedSource())
            {
                state.uiState.playheadSec = clip.startSec + transport_->playheadSec();
                syncEngineIntegrationState("sync via selected clip source");
            }
            else
            {
                logger_->warning("Selected clip source could not be loaded for transport sync: " + assetPath);
                syncEngineIntegrationState("selected clip source load failed");
            }
        }
        else
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
            logger_->warning("Selected clip has no resolvable asset path; transport cleared");
            syncEngineIntegrationState("selected clip asset missing; transport cleared");
        }
        return;
    }

    clearSelectedClipState(state);
    previewPlaybackActive_ = false;
    transport_->clearLoadedSource();
    syncEngineIntegrationState("selected clip no longer exists; transport cleared");
}

void AppController::seekTimelinePlayhead(double timelineSec)
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "seek");
    auto& state = projectManager_->state();
    const auto clampedTimelineSec = std::max(0.0, timelineSec);
    state.uiState.playheadSec = clampedTimelineSec;

    if (previewPlaybackActive_ || shouldUseProjectPreview())
    {
        if (shouldUseProjectPreview() && !previewPlaybackActive_)
        {
            ensureProjectPlaybackRoute();
        }

        if (transport_->sourceDurationSec() > 0.0 || transport_->usingProjectPlayback())
        {
            transport_->seek(std::clamp(clampedTimelineSec, 0.0, transport_->sourceDurationSec()));
            state.uiState.playheadSec = transport_->playheadSec();
            syncEngineIntegrationState(transport_->usingProjectPlayback() ? "timeline seek via live project playback" : "timeline seek via cached project playback");
            return;
        }
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id != state.uiState.selectedClipId)
        {
            continue;
        }

        const auto clipLocalSec = std::clamp(clampedTimelineSec - clip.startSec, 0.0, clip.durationSec);
        transport_->seek(clipLocalSec);
        state.uiState.playheadSec = clip.startSec + transport_->playheadSec();
        syncEngineIntegrationState("timeline seek via selected clip");
        return;
    }

    transport_->seek(clampedTimelineSec);
    syncEngineIntegrationState("timeline seek direct");
}

void AppController::nudgeTimelinePlayhead(double deltaSec)
{
    seekTimelinePlayhead(projectManager_->state().uiState.playheadSec + deltaSec);
}

void AppController::clearSelectedRegion()
{
    timeline_->clearSelectedRegion(projectManager_->state());
}

bool AppController::refreshBackendStatus()
{
    lastBackendStatusRefreshMs_ = juce::Time::getMillisecondCounterHiRes();
    std::string runtimeError;
    const bool runtimeAvailable = localAceStepRuntimeAvailable(&runtimeError);
    const bool usingManagedLocalBackend = settings_.backendUrl.empty() || settings_.backendUrl == std::string(AppConfig::backendUrl);
    bool backendReadyNow = false;
    if (backendProcessManager_ != nullptr)
    {
        if (backendProcessManager_->probeBackendReady())
        {
            backendReadyNow = true;
            aiJobClient_->setBackendUrl(backendProcessManager_->backendUrl());
        }
        else if (runtimeAvailable)
        {
            if (!backendProcessManager_->ownedProcessStillRunning())
            {
                const auto startResult = backendProcessManager_->ensureBackendRunning();
                if (startResult == BackendProcessManager::StartResult::ExternalReady
                    || startResult == BackendProcessManager::StartResult::OwnedLaunched)
                {
                    aiJobClient_->setBackendUrl(backendProcessManager_->backendUrl());
                }
            }
            else
            {
                backendProcessManager_->drainOwnedProcessOutput();
            }
        }
    }

    if (aiJobClient_ != nullptr)
    {
        if (auto* client = dynamic_cast<moon::engine::AIJobClient*>(aiJobClient_.get()))
        {
            client->setBackendReachableHint(backendReadyNow);
        }
    }

    if (usingManagedLocalBackend)
    {
        backendHealth_ = backendReadyNow
            ? moon::engine::HealthResponse{"ok", "managed-local-backend"}
            : moon::engine::HealthResponse{"starting", "managed-local-backend"};
        if (backendReadyNow)
        {
            backendModels_.musicGeneration = {"ace_step"};
        }
        else
        {
            backendModels_ = moon::engine::ModelsResponse{};
        }
        applyRuntimeReadinessOverlay();
        backendFallbackNoticeActive_ = !backendReadyNow;
        refreshStartupNoticeState();
        const auto summary = backendReadyNow ? std::string("managed-local-backend:ready") : std::string("managed-local-backend:starting");
        if (summary != lastBackendStatusSummary_)
        {
            logger_->info("Backend state changed: " + summary);
            lastBackendStatusSummary_ = summary;
        }
        lastBackendReachableState_ = backendReadyNow;
        return backendReadyNow;
    }

    backendHealth_ = aiJobClient_->healthCheck();
    if (aiJobClient_->backendReachable())
    {
        backendModels_ = aiJobClient_->models();
    }
    else
    {
        backendModels_ = moon::engine::ModelsResponse{};
    }
    applyRuntimeReadinessOverlay();

    const bool backendReachable = aiJobClient_->backendReachable();
    backendFallbackNoticeActive_ = !backendReachable;
    refreshStartupNoticeState();
    const auto summary = backendReachable
        ? ("external-backend:" + backendHealth_.status)
        : std::string("external-backend:offline");
    if (summary != lastBackendStatusSummary_)
    {
        logger_->info("Backend state changed: " + summary);
        lastBackendStatusSummary_ = summary;
    }
    lastBackendReachableState_ = backendReachable;
    return backendReachable;
}

void AppController::setBackendUrl(const std::string& backendUrl)
{
    settings_.backendUrl = backendUrl;
    if (aiJobClient_ != nullptr)
    {
        aiJobClient_->setBackendUrl(backendUrl);
    }
    if (backendProcessManager_ != nullptr)
    {
        backendProcessManager_ = std::make_unique<BackendProcessManager>(backendUrl, *logger_);
    }
}

bool AppController::rebuildPreviewPlayback()
{
    runtimeCoordinator_->noteTransportOperation(*projectManager_, "rebuild-preview");
    if (!shouldUseProjectPreview())
    {
        logger_->warning("Preview rebuild skipped because the project has no clips");
        return false;
    }

    const auto wasPlaying = transport_->isPlaying();
    const auto currentPlayhead = projectManager_->state().uiState.playheadSec;
    if (wasPlaying)
    {
        transport_->pause();
    }

    markPreviewPlaybackDirty();
    if (!preparePreviewPlayback())
    {
        logger_->error("Preview rebuild failed");
        return false;
    }

    transport_->seek(std::clamp(currentPlayhead, 0.0, transport_->sourceDurationSec()));
    projectManager_->state().uiState.playheadSec = transport_->playheadSec();

    if (wasPlaying)
    {
        transport_->play();
        previewPlaybackActive_ = true;
    }

    syncEngineIntegrationState("preview cache rebuilt");
    logger_->info("Rebuilt project preview playback cache");
    return true;
}

void AppController::maintainPreviewPlayback()
{
    if (!shouldUseProjectPreview() || transport_->isPlaying())
    {
        return;
    }

    if (shouldUseLiveProjectPlayback())
    {
        if (!transport_->usingProjectPlayback())
        {
            ensureProjectPlaybackRoute();
        }
        return;
    }

    if (!previewPlaybackDirty_
        && transport_->sourcePath() == previewMixPath_.string()
        && transport_->sourceDurationSec() > 0.0)
    {
        return;
    }

    const auto currentPlayhead = projectManager_->state().uiState.playheadSec;
    if (!preparePreviewPlayback())
    {
        return;
    }

    transport_->seek(std::clamp(currentPlayhead, 0.0, transport_->sourceDurationSec()));
    projectManager_->state().uiState.playheadSec = transport_->playheadSec();
    syncEngineIntegrationState("idle cached preview refreshed");
}

void AppController::notifyProjectMixChanged()
{
    markPreviewPlaybackDirty();
    markProjectDirty();
    if (!transport_->isPlaying())
    {
        syncTransportToSelection();
    }
    syncEngineIntegrationState("project mix changed");
}

void AppController::refreshPlaybackUiState()
{
    auto& state = projectManager_->state();
    if (!transport_->isPlaying())
    {
        return;
    }

    if (transport_->usingProjectPlayback() || previewPlaybackActive_ || shouldUseProjectPreview())
    {
        state.uiState.playheadSec = transport_->playheadSec();
        return;
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id == state.uiState.selectedClipId)
        {
            state.uiState.playheadSec = clip.startSec + transport_->playheadSec();
            return;
        }
    }

    state.uiState.playheadSec = transport_->playheadSec();
}

std::string AppController::windowTitle() const
{
    const auto& state = projectManager_->state();
    std::string title = "Moon Audio Editor";
    if (!state.projectName.empty())
    {
        title += " - " + state.projectName;
    }
    if (projectDirty_)
    {
        title += " *";
    }
    return title;
}

void AppController::markPreviewPlaybackDirty()
{
    previewPlaybackDirty_ = true;
    ++previewRenderGeneration_;
    if (previewRenderState_ != nullptr)
    {
        std::lock_guard<std::mutex> lock(previewRenderState_->mutex);
        previewRenderState_->lastError.clear();
    }
}

double AppController::currentTimelinePlayheadSec() const
{
    const auto& state = projectManager_->state();
    if (previewPlaybackActive_ || shouldUseProjectPreview())
    {
        return state.uiState.playheadSec;
    }

    for (const auto& clip : state.clips)
    {
        if (clip.id == state.uiState.selectedClipId)
        {
            return clip.startSec + transport_->playheadSec();
        }
    }

    return transport_->playheadSec();
}

bool AppController::finalizeTimelineEdit(const std::function<bool()>& editOperation,
                                         bool syncTransportToSelectionAfterEdit,
                                         const std::string& failureWarning,
                                         const std::string& operationName)
{
    const bool usingProjectRoute = shouldUseProjectPreview();
    const auto wasPlaying = transport_->isPlaying();
    const auto timelinePlayheadSec = currentTimelinePlayheadSec();
    if (wasPlaying && !usingProjectRoute)
    {
        transport_->pause();
        previewPlaybackActive_ = false;
    }

    const auto updated = editOperation();
    if (!updated)
    {
        if (!failureWarning.empty())
        {
            logger_->warning(failureWarning);
        }
        if (wasPlaying)
        {
            restorePlaybackAfterTimelineEdit(true, timelinePlayheadSec);
        }
        return false;
    }

    runtimeCoordinator_->noteTimelineOperation(*projectManager_, operationName);
    markPreviewPlaybackDirty();
    markProjectDirty();
    if (syncTransportToSelectionAfterEdit && !wasPlaying)
    {
        syncTransportToSelection();
    }
    else if (!shouldUseProjectPreview())
    {
        syncTransportToSelection();
    }
    saveProject();
    if (wasPlaying || syncTransportToSelectionAfterEdit)
    {
        restorePlaybackAfterTimelineEdit(wasPlaying, timelinePlayheadSec);
    }
    return true;
}

void AppController::restorePlaybackAfterTimelineEdit(bool wasPlaying, double timelinePlayheadSec)
{
    auto& state = projectManager_->state();
    state.uiState.playheadSec = std::max(0.0, timelinePlayheadSec);

    if (shouldUseProjectPreview())
    {
        if (shouldUseLiveProjectPlayback())
        {
            if (ensureProjectPlaybackRoute())
            {
                if (!wasPlaying)
                {
                    transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
                    state.uiState.playheadSec = transport_->playheadSec();
                }
                else
                {
                    state.uiState.playheadSec = transport_->playheadSec();
                }

                if (wasPlaying && !transport_->isPlaying())
                {
                    transport_->play();
                }
                syncEngineIntegrationState("live project playback updated after edit");
                return;
            }
        }

        if (wasPlaying)
        {
            previewPlaybackActive_ = true;
            syncEngineIntegrationState("project playback marked stale after edit");
            return;
        }

        if (preparePreviewPlayback())
        {
            transport_->seek(std::clamp(state.uiState.playheadSec, 0.0, transport_->sourceDurationSec()));
            state.uiState.playheadSec = transport_->playheadSec();
            return;
        }
    }

    syncTransportToSelection();
    if (!state.uiState.selectedClipId.empty())
    {
        seekTimelinePlayhead(state.uiState.playheadSec);
    }

    const bool hasPlayableSource = transport_->usingProjectPlayback()
        || (!transport_->sourcePath().empty() && transport_->sourceDurationSec() > 0.0);
    if (wasPlaying && hasPlayableSource)
    {
        transport_->play();
    }
    else if (wasPlaying && !hasPlayableSource)
    {
        logger_->warning("Playback was not resumed after edit because no valid source remained loaded");
    }
}

bool AppController::shouldUseProjectPreview() const
{
    const auto& state = projectManager_->state();
    return !state.clips.empty();
}

bool AppController::shouldUseLiveProjectPlayback() const
{
    return shouldUseProjectPreview()
        && transport_->supportsProjectPlayback()
        && transport_->canUseProjectPlayback();
}

void AppController::requestPreviewPlaybackRender()
{
    if (previewRenderState_ == nullptr || projectManager_ == nullptr || exportService_ == nullptr)
    {
        return;
    }

    pollPreviewPlaybackRender();

    {
        std::lock_guard<std::mutex> lock(previewRenderState_->mutex);
        if (previewRenderState_->active)
        {
            return;
        }
    }

    const auto& state = projectManager_->state();
    const auto durationSec = std::max(0.1, exportService_->estimateMixDuration(state));
    if (durationSec <= 0.0 || projectManager_->cacheDirectory().empty())
    {
        return;
    }

    previewMixPath_ = projectManager_->cacheDirectory() / "timeline_preview_mix.wav";
    const auto generation = previewRenderGeneration_;
    const auto stateCopy = state;
    const auto outputPath = previewMixPath_;
    logger_->info("Queueing background preview render for " + outputPath.string());

    {
        std::lock_guard<std::mutex> lock(previewRenderState_->mutex);
        previewRenderState_->active = true;
        previewRenderState_->generation = generation;
        previewRenderState_->outputPath = outputPath;
        previewRenderState_->durationSec = durationSec;
        previewRenderState_->lastError.clear();
        previewRenderState_->future = std::async(
            std::launch::async,
            [this, stateCopy, outputPath, durationSec, generation]() -> PreviewRenderState::Result
            {
                PreviewRenderState::Result result;
                result.generation = generation;
                result.outputPath = outputPath;
                result.durationSec = durationSec;

                moon::engine::ExportService exporter(*logger_);
                if (!exporter.exportMix(stateCopy, outputPath))
                {
                    result.errorMessage = "Background preview render failed";
                    return result;
                }

                result.success = true;
                return result;
            });
    }
}

bool AppController::pollPreviewPlaybackRender()
{
    if (previewRenderState_ == nullptr)
    {
        return false;
    }

    std::future_status futureStatus = std::future_status::deferred;
    {
        std::lock_guard<std::mutex> lock(previewRenderState_->mutex);
        if (!previewRenderState_->active || !previewRenderState_->future.valid())
        {
            return false;
        }
        futureStatus = previewRenderState_->future.wait_for(std::chrono::milliseconds(0));
    }

    if (futureStatus != std::future_status::ready)
    {
        return false;
    }

    auto result = previewRenderState_->future.get();
    {
        std::lock_guard<std::mutex> lock(previewRenderState_->mutex);
        previewRenderState_->active = false;
        previewRenderState_->lastError = result.errorMessage;
    }

    if (!result.success)
    {
        logger_->warning(
            "Background preview render failed"
            + (result.errorMessage.empty() ? std::string{} : (": " + result.errorMessage)));
        return true;
    }

    if (result.generation != previewRenderGeneration_)
    {
        logger_->info("Discarded stale preview render result");
        return true;
    }

    previewPlaybackDirty_ = false;
    previewMixPath_ = result.outputPath;
    logger_->info("Background preview render completed");
    return true;
}

bool AppController::ensureProjectPlaybackRoute()
{
    if (!shouldUseProjectPreview())
    {
        return false;
    }

    if (shouldUseLiveProjectPlayback())
    {
        transport_->useProjectPlayback(true);
        previewPlaybackActive_ = false;
        previewPlaybackDirty_ = false;
        syncEngineIntegrationState("project playback routed live");
        return transport_->usingProjectPlayback() && transport_->sourceDurationSec() > 0.0;
    }

    if (transport_->usingProjectPlayback())
    {
        transport_->useProjectPlayback(false);
    }

    return preparePreviewPlayback();
}

bool AppController::preparePreviewPlayback()
{
    pollPreviewPlaybackRender();

    auto& state = projectManager_->state();
    const auto previewRouteWasActive = !previewMixPath_.empty()
        && transport_->sourcePath() == previewMixPath_.string();

    if (state.clips.empty())
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        return false;
    }

    const auto durationSec = std::max(0.1, exportService_->estimateMixDuration(state));
    if (durationSec <= 0.0 || projectManager_->cacheDirectory().empty())
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        return false;
    }

    previewMixPath_ = projectManager_->cacheDirectory() / "timeline_preview_mix.wav";
    if (transport_->usingProjectPlayback())
    {
        transport_->useProjectPlayback(false);
    }

    if (previewPlaybackDirty_ || transport_->sourcePath() != previewMixPath_.string() || !std::filesystem::exists(previewMixPath_))
    {
        if (previewPlaybackDirty_ || !std::filesystem::exists(previewMixPath_))
        {
            requestPreviewPlaybackRender();
            if (previewRouteWasActive)
            {
                previewPlaybackActive_ = false;
                transport_->clearLoadedSource();
            }
            return false;
        }
    }

    if (transport_->sourcePath() != previewMixPath_.string()
        || std::abs(transport_->sourceDurationSec() - durationSec) > 0.0001)
    {
        transport_->loadSource(previewMixPath_.string(), durationSec);
    }

    if (!transport_->hasLoadedSource())
    {
        if (previewRouteWasActive)
        {
            previewPlaybackActive_ = false;
            transport_->clearLoadedSource();
        }
        logger_->warning("Project playback preview source could not be loaded");
        return false;
    }

    return true;
}

double AppController::projectPlaybackDurationSec() const
{
    if (shouldUseProjectPreview())
    {
        return std::max(0.1, exportService_->estimateMixDuration(projectManager_->state()));
    }

    if (transport_->sourceDurationSec() > 0.0)
    {
        return transport_->sourceDurationSec();
    }

    if (const auto* clip = findSelectedClip(projectManager_->state()))
    {
        return clip->durationSec;
    }

    return 0.1;
}

bool AppController::setProjectTempo(double tempo)
{
    const auto clampedTempo = std::clamp(tempo, 20.0, 300.0);
    auto& state = projectManager_->state();
    if (std::abs(state.tempo - clampedTempo) < 0.0001)
    {
        return false;
    }

    state.tempo = clampedTempo;
    markPreviewPlaybackDirty();
    syncTransportToSelection();
    syncEngineIntegrationState("project tempo updated");
    markProjectDirty();
    saveProject();
    logger_->info("Updated project tempo to " + std::to_string(clampedTempo) + " BPM");
    return true;
}

bool AppController::setProjectTimeSignature(int numerator, int denominator)
{
    const int sanitizedNumerator = std::clamp(numerator, 2, 12);
    const int sanitizedDenominator = denominator == 8 ? 8 : 4;
    auto& state = projectManager_->state();
    if (state.timeSignatureNumerator == sanitizedNumerator && state.timeSignatureDenominator == sanitizedDenominator)
    {
        return false;
    }

    state.timeSignatureNumerator = sanitizedNumerator;
    state.timeSignatureDenominator = sanitizedDenominator;
    markPreviewPlaybackDirty();
    syncTransportToSelection();
    syncEngineIntegrationState("project time signature updated");
    markProjectDirty();
    saveProject();
    logger_->info("Updated time signature to " + std::to_string(sanitizedNumerator) + "/" + std::to_string(sanitizedDenominator));
    return true;
}

std::vector<std::string> AppController::availableMusicGenerationModels() const
{
    std::vector<std::string> models;
    for (const auto& item : modelRegistrySnapshot_.installed)
    {
        if (item.status == moon::engine::ModelStatus::Ready
            || item.status == moon::engine::ModelStatus::UpdateAvailable
            || item.status == moon::engine::ModelStatus::RuntimeMissing
            || item.status == moon::engine::ModelStatus::RuntimePreparing
            || item.status == moon::engine::ModelStatus::Failed)
        {
            models.push_back(item.id);
        }
    }
    return models;
}

bool AppController::refreshModelRegistry(std::string* errorMessage)
{
    if (modelManager_ == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Model manager is unavailable";
        }
        return false;
    }

    if (!modelManager_->refresh(errorMessage))
    {
        return false;
    }

    modelRegistrySnapshot_ = modelManager_->snapshot();
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::syncRemoteModelCatalog(std::string* errorMessage)
{
    if (modelManager_ == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Model manager is unavailable";
        }
        return false;
    }

    if (!modelManager_->syncRemoteCatalog(errorMessage))
    {
        return false;
    }

    return refreshModelRegistry(errorMessage);
}

bool AppController::pollModelOperations(std::string* errorMessage)
{
    if (modelManager_ == nullptr)
    {
        return false;
    }

    const bool modelChanged = modelManager_->pollOperations(errorMessage);
    if (errorMessage != nullptr && !errorMessage->empty())
    {
        return false;
    }

    const bool runtimeChanged = pollRuntimePreparation(errorMessage);
    const bool runtimeHadError = errorMessage != nullptr && !errorMessage->empty();
    const bool pendingGenerationChanged = pollPendingMusicGeneration();
    if (runtimeHadError && !pendingGenerationChanged)
    {
        return false;
    }
    if (pendingGenerationChanged && errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    auto hasGenerationCapableInstalledModel = [this]()
    {
        for (const auto& item : modelRegistrySnapshot_.installed)
        {
            if (std::find(item.capabilities.begin(), item.capabilities.end(), moon::engine::ModelCapability::SongGeneration) != item.capabilities.end()
                || std::find(item.capabilities.begin(), item.capabilities.end(), moon::engine::ModelCapability::TrackGeneration) != item.capabilities.end())
            {
                return true;
            }
        }
        return false;
    };

    if (runtimePreparePendingAutoStart_ && runtimePrepareState_ != nullptr)
    {
        bool shouldAutoPrepare = false;
        {
            std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
            shouldAutoPrepare = !runtimePrepareState_->active && hasGenerationCapableInstalledModel();
        }

        if (shouldAutoPrepare)
        {
            std::string runtimeDetail;
            const bool runtimeAvailable = localAceStepRuntimeAvailable(&runtimeDetail);
            const bool backendAlreadyReady = backendProcessManager_ != nullptr && backendProcessManager_->probeBackendReady();
            if (!runtimeAvailable && !backendAlreadyReady)
            {
                std::string ignoredError;
                prepareGenerationRuntime(&ignoredError, true);
            }
            runtimePreparePendingAutoStart_ = false;
        }
    }

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const bool shouldRefreshBackendStatus =
        modelChanged
        || runtimeChanged
        || pendingGenerationChanged
      || lastBackendStatusRefreshMs_ <= 0.0
        || (nowMs - lastBackendStatusRefreshMs_) >= 4000.0;
    if (shouldRefreshBackendStatus)
    {
        refreshBackendStatus();
        lastBackendStatusRefreshMs_ = nowMs;
    }

    const bool snapshotUninitialized =
        modelRegistrySnapshot_.installed.empty()
        && modelRegistrySnapshot_.available.empty()
        && modelRegistrySnapshot_.localFolders.empty();
    if (modelChanged || runtimeChanged || pendingGenerationChanged || snapshotUninitialized)
    {
        modelRegistrySnapshot_ = modelManager_->snapshot();
        applyRuntimeReadinessOverlay();
    }

    return modelChanged || runtimeChanged || pendingGenerationChanged;
}

bool AppController::setActiveGenerationModel(moon::engine::ModelCapability capability, const std::string& modelId, std::string& errorMessage)
{
    if (modelManager_ == nullptr || !modelManager_->setActiveModel(capability, modelId, errorMessage))
    {
        return false;
    }

    modelRegistrySnapshot_ = modelManager_->snapshot();
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::addExistingModelFolder(const std::string& modelId, const std::filesystem::path& folderPath, std::string& errorMessage)
{
    if (modelManager_ == nullptr || !modelManager_->addExistingModelFolder(modelId, folderPath, errorMessage))
    {
        return false;
    }

    runtimePreparePendingAutoStart_ = true;
    modelRegistrySnapshot_ = modelManager_->snapshot();
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::verifyInstalledModel(const std::string& modelId, std::string& errorMessage)
{
    if (modelManager_ == nullptr || !modelManager_->verifyModel(modelId, errorMessage))
    {
        return false;
    }

    modelRegistrySnapshot_ = modelManager_->snapshot();
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::removeInstalledModel(const std::string& modelId, std::string& errorMessage)
{
    if (modelManager_ == nullptr || !modelManager_->removeModel(modelId, errorMessage))
    {
        return false;
    }

    modelRegistrySnapshot_ = modelManager_->snapshot();
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::downloadModel(const std::string& modelId, std::string& errorMessage)
{
    if (modelManager_ == nullptr || !modelManager_->downloadModel(modelId, errorMessage))
    {
        return false;
    }

    runtimePreparePendingAutoStart_ = true;
    modelRegistrySnapshot_ = modelManager_->snapshot();
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::updateModel(const std::string& modelId, std::string& errorMessage)
{
    if (modelManager_ == nullptr || !modelManager_->updateModel(modelId, errorMessage))
    {
        return false;
    }

    runtimePreparePendingAutoStart_ = true;
    modelRegistrySnapshot_ = modelManager_->snapshot();
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::cancelAllModelOperations(std::string* errorMessage)
{
    if (modelManager_ == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Model manager is unavailable";
        }
        return false;
    }

    const bool changed = modelManager_->cancelAllModelOperations(errorMessage);
    cancelGenerationRuntimePreparation(nullptr);
    modelRegistrySnapshot_ = modelManager_->snapshot();
    return changed;
}

bool AppController::prepareGenerationRuntime(std::string* errorMessage, bool autoTriggered)
{
    if (runtimePrepareState_ == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Runtime installer is unavailable";
        }
        return false;
    }

    const auto pythonPath = backendPythonPath();
    if (!std::filesystem::exists(pythonPath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Embedded backend Python was not found";
        }
        return false;
    }

    const auto logPath = runtimeInstallLogPath();
    resetRuntimeInstallLog(logPath);
    appendRuntimeInstallLog(logPath, "runtime install requested");

    {
        std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
        if (runtimePrepareState_->active)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Runtime preparation is already running";
            }
            return false;
        }

        runtimePrepareState_->active = true;
        runtimePrepareState_->cancelRequested = false;
        runtimePrepareState_->phase = RuntimePrepareState::Phase::Checking;
        runtimePrepareState_->progress = 0.02;
        runtimePrepareState_->message = autoTriggered ? "Preparing runtime automatically" : "Preparing runtime";
        runtimePrepareState_->lastError.clear();
        runtimePrepareState_->recentOutput.clear();
        runtimePrepareState_->sourceDescription.clear();
        runtimePrepareState_->logPath = logPath;
        runtimePrepareState_->process.reset();
    }

    logger_->info("ACE-Step runtime preparation requested");
    runtimePreparePendingAutoStart_ = false;
    runtimePrepareState_->future = std::async(std::launch::async, [this]() -> RuntimePrepareState::Result
    {
        RuntimePrepareState::Result result;
        auto setStage = [this](RuntimePrepareState::Phase phase, double progress, const std::string& message)
        {
            runtimePrepareState_->setPhase(phase, progress, message);
            logger_->info("Runtime prepare stage: " + message);
            appendRuntimeInstallLog(runtimeInstallLogPath(), "[stage] " + message);
        };

        auto fail = [this, &result](const std::string& message)
        {
            result.success = false;
            result.errorMessage = message;
            runtimePrepareState_->setPhase(RuntimePrepareState::Phase::Failed, 1.0, message);
            {
                std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
                runtimePrepareState_->lastError = message;
            }
            logger_->error("ACE-Step runtime preparation failed: " + message);
            appendRuntimeInstallLog(runtimeInstallLogPath(), "[failed] " + message);
            return result;
        };

        auto cancelled = [this, &result]()
        {
            result.cancelled = true;
            result.errorMessage = "Runtime preparation cancelled";
            runtimePrepareState_->setPhase(RuntimePrepareState::Phase::Cancelled, 1.0, "Runtime preparation cancelled");
            logger_->warning("ACE-Step runtime preparation cancelled");
            appendRuntimeInstallLog(runtimeInstallLogPath(), "[cancelled] Runtime preparation cancelled");
            return result;
        };

        if (runtimePrepareState_->cancelled())
        {
            return cancelled();
        }

        setStage(RuntimePrepareState::Phase::Checking, 0.04, "Checking local AI runtime");
        const auto preflightValidation = validateLocalAceStepRuntime();
        appendRuntimeInstallLog(runtimeInstallLogPath(), "preflight validation stage: " + runtimeValidationStageLabel(preflightValidation.stage));
        appendRuntimeInstallLog(runtimeInstallLogPath(), "preflight validation summary: " + preflightValidation.summary);
        appendRuntimeInstallLog(runtimeInstallLogPath(), "preflight validation detail: " + preflightValidation.detail);
        if (preflightValidation.stage == RuntimeValidationStage::Success)
        {
            if (backendProcessManager_ == nullptr || aiJobClient_ == nullptr)
            {
                return fail("Backend runtime services are unavailable");
            }

            {
                std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
                runtimePrepareState_->sourceDescription = preflightValidation.entrypointDescription.empty()
                    ? "existing validated ACE-Step runtime"
                    : preflightValidation.entrypointDescription;
            }

            setStage(RuntimePrepareState::Phase::RestartingBackend, 0.86, "Starting embedded generation backend");
            if (!backendProcessManager_->probeBackendReady() && !backendProcessManager_->ownedProcessStillRunning())
            {
                const auto startResult = backendProcessManager_->ensureBackendRunning();
                if (startResult == BackendProcessManager::StartResult::LauncherMissing
                    || startResult == BackendProcessManager::StartResult::LaunchFailed)
                {
                    return fail(backendProcessManager_->lastError().empty()
                        ? std::string("Embedded backend could not be started")
                        : backendProcessManager_->lastError());
                }
            }

            aiJobClient_->setBackendUrl(backendProcessManager_->backendUrl());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (runtimePrepareState_->cancelled())
                {
                    if (backendProcessManager_ != nullptr)
                    {
                        backendProcessManager_->shutdownOwnedBackend();
                    }
                    return cancelled();
                }

                if (backendProcessManager_->probeBackendReady())
                {
                    result.success = true;
                    runtimePrepareState_->setPhase(RuntimePrepareState::Phase::Completed, 1.0, "Runtime ready");
                    logger_->info("Embedded backend startup completed successfully");
                    appendRuntimeInstallLog(runtimeInstallLogPath(), "[completed] existing runtime backend ready");
                    return result;
                }

                backendProcessManager_->drainOwnedProcessOutput();
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }

            backendProcessManager_->captureOwnedProcessOutput();
            return fail(backendProcessManager_->lastError().empty()
                ? std::string("Local generation backend startup timed out")
                : backendProcessManager_->lastError());
        }

        setStage(RuntimePrepareState::Phase::ResolvingSource, 0.08, "Resolving ACE-Step runtime source");
        const auto installPlan = resolveAceStepRuntimeInstallPlan();
        {
            std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
            runtimePrepareState_->sourceDescription = installPlan.sourceDescription;
        }
        logger_->info("ACE-Step runtime source: " + installPlan.sourceDescription);
        appendRuntimeInstallLog(runtimeInstallLogPath(), "selected install source: " + installPlan.sourceDescription);
        for (const auto& line : installPlan.resolutionLog)
        {
            logger_->info("ACE-Step runtime source resolution: " + line);
            appendRuntimeInstallLog(runtimeInstallLogPath(), "source resolution: " + line);
        }

        if (runtimePrepareState_->cancelled())
        {
            return cancelled();
        }

        setStage(RuntimePrepareState::Phase::PreparingEnv, 0.16, "Preparing backend environment");
        {
            juce::StringArray bootstrapCommand;
            bootstrapCommand.add(backendPythonPath().string());
            bootstrapCommand.add("-m");
            bootstrapCommand.add("pip");
            bootstrapCommand.add("install");
            bootstrapCommand.add("--disable-pip-version-check");
            bootstrapCommand.add("--no-input");
            bootstrapCommand.add("--upgrade");
            bootstrapCommand.add("pip");
            bootstrapCommand.add("setuptools");
            bootstrapCommand.add("wheel");
            const auto bootstrapResult = runCommandCapture(bootstrapCommand, 240000);
            appendRuntimeInstallLog(runtimeInstallLogPath(), "bootstrap command: " + bootstrapCommand.joinIntoString(" ").toStdString());
            appendRuntimeInstallLog(runtimeInstallLogPath(), bootstrapResult.output);
            if (!bootstrapResult.started || bootstrapResult.timedOut || bootstrapResult.exitCode != 0)
            {
                return fail("Embedded backend pip bootstrap failed. See " + runtimeInstallLogPath().string());
            }
        }

        if (runtimePrepareState_->cancelled())
        {
            return cancelled();
        }

        setStage(RuntimePrepareState::Phase::Downloading, 0.26, "Downloading runtime packages");
        auto process = std::make_shared<juce::ChildProcess>();
        {
            std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
            runtimePrepareState_->process = process;
        }

        juce::StringArray commandLine;
        for (const auto& part : installPlan.command)
        {
            commandLine.add(part);
        }

        appendRuntimeInstallLog(runtimeInstallLogPath(), "install command: " + commandLine.joinIntoString(" ").toStdString());

        if (!process->start(commandLine))
        {
            return fail("Could not launch runtime installer. See " + runtimeInstallLogPath().string());
        }

        auto updateFromOutput = [this, &setStage](const std::string& chunk, double fallbackProgress)
        {
            runtimePrepareState_->appendOutput(chunk);
            appendRuntimeInstallLog(runtimeInstallLogPath(), chunk);
            auto lower = chunk;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (lineContainsAny(lower, {"collecting", "resolving"}))
            {
                setStage(RuntimePrepareState::Phase::Downloading, 0.34, "Resolving runtime dependencies");
            }
            else if (lineContainsAny(lower, {"downloading", "fetching"}))
            {
                setStage(RuntimePrepareState::Phase::Downloading, 0.46, "Downloading runtime");
            }
            else if (lineContainsAny(lower, {"building wheel", "preparing metadata"}))
            {
                setStage(RuntimePrepareState::Phase::Installing, 0.60, "Building runtime package");
            }
            else if (lineContainsAny(lower, {"installing collected packages", "successfully installed"}))
            {
                setStage(RuntimePrepareState::Phase::Installing, 0.74, "Installing runtime");
            }
            else
            {
                setStage(RuntimePrepareState::Phase::Installing, fallbackProgress, "Installing runtime");
            }
        };

        const auto installStart = juce::Time::getMillisecondCounterHiRes();
        while (process->isRunning())
        {
            if (runtimePrepareState_->cancelled())
            {
                process->kill();
                return cancelled();
            }

            char buffer[2048]{};
            const auto bytesRead = process->readProcessOutput(buffer, static_cast<int>(sizeof(buffer)));
            if (bytesRead > 0)
            {
                const std::string chunk(buffer, static_cast<std::size_t>(bytesRead));
                const auto elapsedSec = (juce::Time::getMillisecondCounterHiRes() - installStart) / 1000.0;
                const auto fallbackProgress = std::min(0.72, 0.28 + elapsedSec * 0.01);
                updateFromOutput(chunk, fallbackProgress);
                logger_->info("ACE-Step installer output: " + chunk);
            }
            else
            {
                const auto elapsedSec = (juce::Time::getMillisecondCounterHiRes() - installStart) / 1000.0;
                const auto fallbackProgress = std::min(0.72, 0.28 + elapsedSec * 0.01);
                setStage(RuntimePrepareState::Phase::Installing, fallbackProgress, "Installing runtime");
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        }

        const auto trailingOutput = process->readAllProcessOutput().toStdString();
        if (!trailingOutput.empty())
        {
            runtimePrepareState_->appendOutput(trailingOutput);
            appendRuntimeInstallLog(runtimeInstallLogPath(), trailingOutput);
            logger_->info("ACE-Step installer final output: " + trailingOutput);
        }

        {
            std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
            runtimePrepareState_->process.reset();
        }

        if (process->getExitCode() != 0)
        {
            std::string outputExcerpt;
            {
                std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
                outputExcerpt = runtimePrepareState_->recentOutput;
            }
            if (outputExcerpt.size() > 600)
            {
                outputExcerpt.erase(0, outputExcerpt.size() - 600);
            }
            return fail(
                "Runtime installer failed from " + installPlan.sourceDescription
                + " (exit " + std::to_string(process->getExitCode()) + "). "
                + (outputExcerpt.empty() ? "" : ("Last output: " + outputExcerpt + " "))
                + "See " + runtimeInstallLogPath().string());
        }

        if (runtimePrepareState_->cancelled())
        {
            return cancelled();
        }

        setStage(RuntimePrepareState::Phase::Validating, 0.84, "Validating installed runtime");
        const auto validationReport = validateLocalAceStepRuntime();
        appendRuntimeInstallLog(runtimeInstallLogPath(), "validation stage: " + runtimeValidationStageLabel(validationReport.stage));
        appendRuntimeInstallLog(runtimeInstallLogPath(), "validation summary: " + validationReport.summary);
        appendRuntimeInstallLog(runtimeInstallLogPath(), "validation detail: " + validationReport.detail);
        if (validationReport.stage != RuntimeValidationStage::Success)
        {
            return fail(
                "Runtime validation failed at "
                + runtimeValidationStageLabel(validationReport.stage)
                + ": "
                + (validationReport.detail.empty() ? validationReport.summary : validationReport.detail)
                + ". See "
                + runtimeInstallLogPath().string());
        }

        setStage(RuntimePrepareState::Phase::RestartingBackend, 0.92, "Restarting embedded backend");
        if (backendProcessManager_ != nullptr)
        {
            backendProcessManager_->shutdownOwnedBackend();
        }

        std::string backendError;
        if (!ensureManagedBackendReady(&backendError, true))
        {
            appendRuntimeInstallLog(runtimeInstallLogPath(), "backend health failure: " + backendError);
            return fail(
                "Runtime validation failed at healthcheck_failed: "
                + (backendError.empty() ? std::string("backend health check failed after runtime install") : backendError)
                + ". See "
                + runtimeInstallLogPath().string());
        }

        result.success = true;
        runtimePrepareState_->setPhase(RuntimePrepareState::Phase::Completed, 1.0, "Runtime ready");
        logger_->info("ACE-Step runtime preparation completed successfully");
        appendRuntimeInstallLog(runtimeInstallLogPath(), "[completed] runtime ready");
        return result;
    });

    modelRegistrySnapshot_ = modelManager_ != nullptr ? modelManager_->snapshot() : modelRegistrySnapshot_;
    applyRuntimeReadinessOverlay();
    return true;
}

bool AppController::cancelGenerationRuntimePreparation(std::string* errorMessage)
{
    if (runtimePrepareState_ == nullptr)
    {
        return false;
    }

    std::shared_ptr<juce::ChildProcess> process;
    {
        std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
        if (!runtimePrepareState_->active)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Runtime preparation is not running";
            }
            return false;
        }
        runtimePrepareState_->cancelRequested = true;
        process = runtimePrepareState_->process;
    }

    if (process != nullptr && process->isRunning())
    {
        process->kill();
    }

    return true;
}

bool AppController::cancelTask(const std::string& jobId)
{
    if (jobId.empty())
    {
        return false;
    }

    if (!pendingMusicGenerationTaskId_.empty() && jobId == pendingMusicGenerationTaskId_)
    {
        bool cancelled = false;
        if (!pendingMusicGenerationRealJobId_.empty())
        {
            cancelled = taskManager_->cancelTask(pendingMusicGenerationRealJobId_);
        }
        else
        {
            cancelled = cancelGenerationRuntimePreparation(nullptr);
        }

        taskManager_->upsertTask(moon::engine::TaskInfo{
            pendingMusicGenerationTaskId_,
            "music-generation",
            "cancelled",
            1.0,
            "Generation cancelled"});
        pendingMusicGenerationRequest_.reset();
        pendingMusicGenerationRealJobId_.clear();
        pendingMusicGenerationTaskId_.clear();
        if (cancelled)
        {
            markProjectDirty();
        }
        return true;
    }

    const auto cancelled = taskManager_->cancelTask(jobId);
    if (cancelled)
    {
        markProjectDirty();
    }
    return cancelled;
}

std::filesystem::path AppController::modelsRootDirectory() const
{
    return modelManager_ != nullptr ? modelManager_->rootPath() : (std::filesystem::path("data") / "models");
}

RuntimeInstallDiagnostics AppController::runtimeInstallDiagnostics() const
{
    RuntimeInstallDiagnostics diagnostics;
    if (runtimePrepareState_ == nullptr)
    {
        return diagnostics;
    }

    std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
    diagnostics.active = runtimePrepareState_->active;
    diagnostics.progress = runtimePrepareState_->progress;
    diagnostics.source = runtimePrepareState_->sourceDescription;
    diagnostics.summary = runtimePrepareState_->message;
    diagnostics.error = runtimePrepareState_->lastError;
    diagnostics.logPath = runtimePrepareState_->logPath.string();
    diagnostics.hasLog = !diagnostics.logPath.empty() && std::filesystem::exists(runtimePrepareState_->logPath);
    return diagnostics;
}

bool AppController::revealRuntimeInstallLog() const
{
    const auto diagnostics = runtimeInstallDiagnostics();
    if (!diagnostics.hasLog)
    {
        return false;
    }

    juce::File(diagnostics.logPath).revealToUser();
    return true;
}

bool AppController::canCloseSafely() const
{
    return taskManager_->activeTaskCount() == 0;
}

void AppController::prepareForShutdown()
{
    cancelGenerationRuntimePreparation(nullptr);
    if (backendProcessManager_ != nullptr)
    {
        backendProcessManager_->shutdownOwnedBackend();
    }
    autosaveIfNeeded();
    saveProject();
    logger_->info("Prepared application state for shutdown");
}

bool AppController::ensureManagedBackendReady(std::string* errorMessage, bool forceStart)
{
    std::string runtimeDetail;
    if (!localAceStepRuntimeAvailable(&runtimeDetail))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = runtimeDetail;
        }
        return false;
    }

    if (backendProcessManager_ == nullptr || aiJobClient_ == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Backend runtime services are unavailable";
        }
        return false;
    }

    if (backendProcessManager_->probeBackendReady())
    {
        aiJobClient_->setBackendUrl(backendProcessManager_->backendUrl());
        return true;
    }

    if (!forceStart && !backendProcessManager_->ownedProcessStillRunning())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = backendProcessManager_->lastError().empty() ? "Local backend is not running" : backendProcessManager_->lastError();
        }
        return false;
    }

    const auto startResult = backendProcessManager_->ensureBackendRunning();
    if (startResult == BackendProcessManager::StartResult::LauncherMissing
        || startResult == BackendProcessManager::StartResult::LaunchFailed)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = backendProcessManager_->lastError();
        }
        return false;
    }

    aiJobClient_->setBackendUrl(backendProcessManager_->backendUrl());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (backendProcessManager_->probeBackendReady())
        {
            return true;
        }

        backendProcessManager_->drainOwnedProcessOutput();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    backendProcessManager_->captureOwnedProcessOutput();
    if (errorMessage != nullptr)
    {
        *errorMessage = backendProcessManager_->lastError().empty()
            ? "Local generation backend startup timed out"
            : backendProcessManager_->lastError();
    }
    return false;
}

bool AppController::pollRuntimePreparation(std::string* errorMessage)
{
    if (runtimePrepareState_ == nullptr)
    {
        return false;
    }

    std::future_status futureStatus = std::future_status::deferred;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
        active = runtimePrepareState_->active;
        if (runtimePrepareState_->future.valid())
        {
            futureStatus = runtimePrepareState_->future.wait_for(std::chrono::milliseconds(0));
        }
    }

    if (!active)
    {
        return false;
    }

    if (futureStatus != std::future_status::ready)
    {
        modelRegistrySnapshot_ = modelManager_ != nullptr ? modelManager_->snapshot() : modelRegistrySnapshot_;
        applyRuntimeReadinessOverlay();
        return true;
    }

    auto result = runtimePrepareState_->future.get();
    {
        std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
        runtimePrepareState_->active = false;
        runtimePrepareState_->process.reset();
        if (!result.success && !result.cancelled)
        {
            runtimePrepareState_->lastError = result.errorMessage;
        }
    }

    if (result.cancelled)
    {
        logger_->warning("Runtime preparation finished as cancelled");
    }
    else if (!result.success)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = result.errorMessage;
        }
    }

    modelRegistrySnapshot_ = modelManager_ != nullptr ? modelManager_->snapshot() : modelRegistrySnapshot_;
    refreshBackendStatus();
      applyRuntimeReadinessOverlay();
      return true;
  }

bool AppController::pollPendingMusicGeneration()
{
    if (pendingMusicGenerationTaskId_.empty() || !pendingMusicGenerationRequest_.has_value())
    {
        return false;
    }

    bool changed = false;
    if (pendingMusicGenerationRealJobId_.empty())
    {
        std::string runtimePollError;
        if (pollRuntimePreparation(&runtimePollError))
        {
            changed = true;
        }

        std::string runtimeDetail;
        const bool runtimeAvailable = localAceStepRuntimeAvailable(&runtimeDetail);
        const bool backendReachable = backendProcessManager_ != nullptr && backendProcessManager_->probeBackendReady();
        RuntimeInstallDiagnostics diagnostics = runtimeInstallDiagnostics();
        if (diagnostics.active && !backendReachable)
        {
            taskManager_->upsertTask(moon::engine::TaskInfo{
                pendingMusicGenerationTaskId_,
                "music-generation",
                "running",
                std::clamp(0.02 + diagnostics.progress * 0.20, 0.02, 0.22),
                diagnostics.summary.empty() ? std::string("Preparing local AI runtime") : diagnostics.summary});
            changed = true;
            return changed;
        }

        if (!runtimeAvailable && !backendReachable)
        {
            if (!diagnostics.error.empty())
            {
                taskManager_->upsertTask(moon::engine::TaskInfo{
                    pendingMusicGenerationTaskId_,
                    "music-generation",
                    "failed",
                    1.0,
                    diagnostics.error});
                pendingMusicGenerationRequest_.reset();
                pendingMusicGenerationTaskId_.clear();
                return true;
            }

            std::string prepareError;
            const bool prepareStarted = prepareGenerationRuntime(&prepareError, true);
            if (!prepareStarted && prepareError != "Runtime preparation is already running")
            {
                taskManager_->upsertTask(moon::engine::TaskInfo{
                    pendingMusicGenerationTaskId_,
                    "music-generation",
                    "failed",
                    1.0,
                    prepareError.empty() ? std::string("Could not prepare local AI runtime") : prepareError});
                pendingMusicGenerationRequest_.reset();
                pendingMusicGenerationTaskId_.clear();
                return true;
            }

            taskManager_->upsertTask(moon::engine::TaskInfo{
                pendingMusicGenerationTaskId_,
                "music-generation",
                "running",
                0.20,
                "Preparing local AI runtime"});
            return true;
        }

        if (!backendReachable)
        {
            std::string prepareError;
            const bool prepareStarted = prepareGenerationRuntime(&prepareError, true);
            if (!prepareStarted && prepareError != "Runtime preparation is already running")
            {
                taskManager_->upsertTask(moon::engine::TaskInfo{
                    pendingMusicGenerationTaskId_,
                    "music-generation",
                    "failed",
                    1.0,
                    prepareError.empty() ? std::string("Could not start local AI backend") : prepareError});
                pendingMusicGenerationRequest_.reset();
                pendingMusicGenerationTaskId_.clear();
                return true;
            }

            taskManager_->upsertTask(moon::engine::TaskInfo{
                pendingMusicGenerationTaskId_,
                "music-generation",
                "running",
                0.20,
                "Starting local AI backend"});
            return true;
        }

        if (aiJobClient_ != nullptr && backendProcessManager_ != nullptr)
        {
            aiJobClient_->setBackendUrl(backendProcessManager_->backendUrl());
        }

        taskManager_->upsertTask(moon::engine::TaskInfo{
            pendingMusicGenerationTaskId_,
            "music-generation",
            "running",
            0.24,
            "Runtime ready, starting generation"});

        auto queuedJobId = queueResolvedMusicGeneration(*pendingMusicGenerationRequest_);
        if (!queuedJobId.has_value())
        {
            const auto message = lastMusicGenerationError_.empty()
                ? std::string("Generation could not start after runtime preparation")
                : lastMusicGenerationError_;
            taskManager_->upsertTask(moon::engine::TaskInfo{
                pendingMusicGenerationTaskId_,
                "music-generation",
                "failed",
                1.0,
                message});
            pendingMusicGenerationRequest_.reset();
            pendingMusicGenerationTaskId_.clear();
            return true;
        }

        pendingMusicGenerationRealJobId_ = *queuedJobId;
        return true;
    }

    const auto tasksSnapshot = taskManager_->tasks();
    const auto realIt = tasksSnapshot.find(pendingMusicGenerationRealJobId_);
    if (realIt == tasksSnapshot.end())
    {
        return false;
    }

    const auto& realTask = realIt->second;
    auto mirrored = realTask;
    mirrored.id = pendingMusicGenerationTaskId_;
    mirrored.type = "music-generation";
    mirrored.progress = std::clamp(0.25 + realTask.progress * 0.75, 0.25, 1.0);
    if (realTask.status == "queued" || realTask.status == "running" || realTask.status == "cancelling")
    {
        mirrored.status = realTask.status;
        mirrored.message = realTask.message.empty() ? std::string("Generating audio") : realTask.message;
    }
    taskManager_->upsertTask(mirrored);
    changed = true;

    if (realTask.status == "completed" || realTask.status == "failed" || realTask.status == "cancelled")
    {
        pendingMusicGenerationRequest_.reset();
        pendingMusicGenerationRealJobId_.clear();
        pendingMusicGenerationTaskId_.clear();
    }

    return changed;
}

void AppController::applyRuntimeReadinessOverlay()
{
    std::string runtimeDetail;
    (void) localAceStepRuntimeAvailable(&runtimeDetail);
    const bool backendReachable = aiJobClient_ != nullptr && aiJobClient_->backendReachable();
    const bool runtimeReady = backendReachable;
    const bool runtimeStarting = backendProcessManager_ != nullptr && backendProcessManager_->ownedProcessStillRunning() && !runtimeReady;
    bool runtimeInstallActive = false;
    bool runtimeInstallFailed = false;
    bool runtimeInstallCancelled = false;
    double runtimeInstallProgress = 0.0;
    std::string runtimeInstallMessage;
    std::string runtimeInstallError;
    if (runtimePrepareState_ != nullptr)
    {
        std::lock_guard<std::mutex> lock(runtimePrepareState_->mutex);
        runtimeInstallActive = runtimePrepareState_->active;
        runtimeInstallProgress = runtimePrepareState_->progress;
        runtimeInstallMessage = runtimePrepareState_->message;
        runtimeInstallError = runtimePrepareState_->lastError;
        runtimeInstallFailed = runtimePrepareState_->phase == RuntimePrepareState::Phase::Failed;
        runtimeInstallCancelled = runtimePrepareState_->phase == RuntimePrepareState::Phase::Cancelled;
    }

    const std::string runtimeHint = backendProcessManager_ == nullptr
        ? "Embedded backend manager is unavailable"
        : (backendProcessManager_->lastError().empty()
            ? runtimeDetail
            : backendProcessManager_->lastError());

    for (auto& item : modelRegistrySnapshot_.installed)
    {
        const bool generationCapable = std::find(item.capabilities.begin(), item.capabilities.end(), moon::engine::ModelCapability::SongGeneration) != item.capabilities.end()
            || std::find(item.capabilities.begin(), item.capabilities.end(), moon::engine::ModelCapability::TrackGeneration) != item.capabilities.end();
        if (!generationCapable)
        {
            continue;
        }

        if (item.status == moon::engine::ModelStatus::Ready || item.status == moon::engine::ModelStatus::UpdateAvailable)
        {
            if (runtimeInstallActive && !runtimeReady)
            {
                item.status = moon::engine::ModelStatus::RuntimePreparing;
                item.operationProgress = runtimeInstallProgress;
                item.operationStatusText = runtimeInstallMessage.empty() ? "Preparing embedded generation runtime" : runtimeInstallMessage;
            }
            else if (runtimeInstallFailed)
            {
                item.status = moon::engine::ModelStatus::Failed;
                item.operationProgress = 1.0;
                item.operationStatusText = runtimeInstallError.empty() ? "Runtime install failed" : runtimeInstallError;
            }
            else if (runtimeInstallCancelled)
            {
                item.status = moon::engine::ModelStatus::RuntimeMissing;
                item.operationProgress = 0.0;
                item.operationStatusText = "Runtime install cancelled";
            }
            else if (runtimeStarting)
            {
                item.status = moon::engine::ModelStatus::RuntimePreparing;
                item.operationProgress = 0.92;
                item.operationStatusText = "Starting embedded generation backend";
            }
            else if (!runtimeReady)
            {
                item.status = moon::engine::ModelStatus::RuntimeMissing;
                item.operationProgress = 0.0;
                item.operationStatusText = runtimeHint;
            }
            else if (item.operationStatusText.empty() || item.operationStatusText == "Ready")
            {
                item.operationProgress = 1.0;
                item.operationStatusText = "Ready for local generation";
            }
        }
    }
}

std::string AppController::startupNotice() const
{
    std::string notice;
    if (autosaveRecoveryNoticeActive_)
    {
        notice += "Recovered project state from autosave.project.json in the default workspace project.";
    }
    return notice;
}

void AppController::clearStartupNotice()
{
    backendFallbackNoticeActive_ = false;
    autosaveRecoveryNoticeActive_ = false;
}

void AppController::markProjectDirty()
{
    projectDirty_ = true;
}

void AppController::clearProjectDirty()
{
    projectDirty_ = false;
}

void AppController::refreshStartupNoticeState()
{
    if (aiJobClient_ != nullptr && aiJobClient_->backendReachable())
    {
        backendFallbackNoticeActive_ = false;
    }

    if (projectManager_ != nullptr && !projectManager_->hasAutosave())
    {
        autosaveRecoveryNoticeActive_ = false;
    }
}

void AppController::syncEngineIntegrationState(const std::string& reason)
{
    runtimeCoordinator_->sync(*projectManager_, *timeline_, *transport_, reason);
    logPlaybackRouteTransition(reason);
}

void AppController::logPlaybackRouteTransition(const std::string& reason)
{
    const auto currentRoute = transport_->playbackRouteSummary();
    const auto currentDiagnostic = transport_->projectPlaybackDiagnostic();
    if (currentRoute == lastPlaybackRouteSummary_ && currentDiagnostic == lastPlaybackDiagnostic_)
    {
        return;
    }

    std::string message = "Playback route transition: ";
    if (lastPlaybackRouteSummary_.empty())
    {
        message += "<unset>";
    }
    else
    {
        message += lastPlaybackRouteSummary_;
    }
    message += " -> " + currentRoute;

    if (!reason.empty())
    {
        message += " | reason=" + reason;
    }

    if (currentRoute == "project-live")
    {
        logger_->info(message + " | live mix source active");
    }
    else if (currentRoute == "project-cached-preview")
    {
        logger_->warning(message + " | cached preview fallback active | diagnostic=" + currentDiagnostic);
    }
    else if (currentRoute == "selected-source")
    {
        logger_->info(message + " | selected clip source active");
    }
    else
    {
        logger_->warning(message + " | no valid playback source loaded | diagnostic=" + currentDiagnostic);
    }

    lastPlaybackRouteSummary_ = currentRoute;
    lastPlaybackDiagnostic_ = currentDiagnostic;
}
}




