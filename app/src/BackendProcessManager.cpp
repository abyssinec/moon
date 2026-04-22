#include "BackendProcessManager.h"

#include <array>
#include <limits>
#include <set>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include "AppConfig.h"

namespace moon::app
{
namespace
{
bool canConnectToBackendPort(int port, int timeoutMs)
{
#if defined(_WIN32)
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    bool connected = false;
    const auto connectResult = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (connectResult == 0)
    {
        connected = true;
    }
    else
    {
        const auto error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINVAL)
        {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(sock, &writeSet);
            TIMEVAL timeout{};
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = (timeoutMs % 1000) * 1000;
            if (select(0, nullptr, &writeSet, nullptr, &timeout) > 0)
            {
                int socketError = 0;
                int socketErrorSize = sizeof(socketError);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorSize) == 0
                    && socketError == 0)
                {
                    connected = true;
                }
            }
        }
    }

    closesocket(sock);
    WSACleanup();
    return connected;
#else
    juce::ignoreUnused(port, timeoutMs);
    return false;
#endif
}

int parsePortFromBackendUrl(const std::string& backendUrl)
{
    auto schemePos = backendUrl.find("://");
    auto authorityStart = schemePos == std::string::npos ? 0 : schemePos + 3;
    auto authorityEnd = backendUrl.find('/', authorityStart);
    const auto authority = backendUrl.substr(authorityStart, authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart);
    const auto colonPos = authority.rfind(':');
    if (colonPos == std::string::npos)
    {
        return 8000;
    }

    try
    {
        return std::clamp(std::stoi(authority.substr(colonPos + 1)), 1, 65535);
    }
    catch (...)
    {
        return 8000;
    }
}

std::string makeBackendUrlForPort(int port)
{
    return "http://127.0.0.1:" + std::to_string(port);
}

bool isPortAvailable(int port)
{
#if defined(_WIN32)
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return port == 8000;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        WSACleanup();
        return port == 8000;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    const bool available = bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    closesocket(sock);
    WSACleanup();
    return available;
#else
    juce::ignoreUnused(port);
    return true;
#endif
}
}

BackendProcessManager::BackendProcessManager(std::string backendUrl, moon::engine::Logger& logger)
    : logger_(logger)
    , probeClient_(std::move(backendUrl), logger)
{
}

BackendProcessManager::~BackendProcessManager()
{
    shutdownOwnedBackend();
}

void BackendProcessManager::captureOwnedProcessOutput()
{
    if (ownedBackendProcess_ == nullptr)
    {
        return;
    }

    const auto output = ownedBackendProcess_->readAllProcessOutput().toStdString();
    if (!output.empty())
    {
        lastProcessOutput_ = output;
        logger_.error("Backend process output:\n" + output);
    }
}

void BackendProcessManager::drainOwnedProcessOutput()
{
    if (ownedBackendProcess_ == nullptr)
    {
        return;
    }

    std::string output;
    std::array<char, 2048> buffer{};
    for (;;)
    {
        const auto bytesRead = ownedBackendProcess_->readProcessOutput(buffer.data(), static_cast<int>(buffer.size()));
        if (bytesRead <= 0)
        {
            break;
        }
        output.append(buffer.data(), static_cast<std::size_t>(bytesRead));
    }

    if (!output.empty())
    {
        lastProcessOutput_ += output;
    }
}

bool BackendProcessManager::lastProcessOutputSuggestsPortInUse() const
{
    return lastProcessOutput_.find("10048") != std::string::npos
        || lastProcessOutput_.find("address already in use") != std::string::npos
        || lastProcessOutput_.find("Only one usage of each socket address") != std::string::npos;
}

bool BackendProcessManager::probeBackendReady()
{
    lastError_.clear();
    const auto port = parsePortFromBackendUrl(probeClient_.backendUrl());
    if (!canConnectToBackendPort(port, 40))
    {
        return false;
    }
    return true;
}

BackendProcessManager::StartResult BackendProcessManager::ensureBackendRunning()
{
    if (probeBackendReady())
    {
        backendWasExternal_ = true;
        ownsBackendProcess_ = false;
        logger_.info("Backend already reachable; treating it as external");
        return StartResult::ExternalReady;
    }

    auto backendRoot = locateBackendRoot();
    if (!backendRoot.has_value())
    {
        lastError_ = "Backend root was not found near the executable or workspace.";
        logger_.error(lastError_);
        return StartResult::LauncherMissing;
    }

    backendRootPath_ = *backendRoot;
    pythonExecutablePath_ = locatePythonExecutable(backendRootPath_);
    lastProcessOutput_.clear();
    const auto launchPort = selectOwnedBackendPort();
    probeClient_.setBackendUrl(makeBackendUrlForPort(launchPort));
    logger_.info("Resolved backend root: " + backendRootPath_.string());
    logger_.info("Resolved backend python: " + pythonExecutablePath_.string());
    logger_.info("Resolved backend URL: " + probeClient_.backendUrl());
    launcherCommand_ = buildLaunchCommand(backendRootPath_, pythonExecutablePath_, launchPort);
    logger_.info("Launching backend command: " + launcherCommand_);
    const auto launcherArguments = buildLaunchArguments(backendRootPath_, pythonExecutablePath_, launchPort);

    ownedBackendProcess_ = std::make_unique<juce::ChildProcess>();
    if (!ownedBackendProcess_->start(launcherArguments))
    {
        ownedBackendProcess_.reset();
        lastError_ = "Backend launcher command could not be started.";
        logger_.error(lastError_);
        return StartResult::LaunchFailed;
    }

    ownsBackendProcess_ = true;
    backendWasExternal_ = false;
    logger_.info("Backend process launched from root: " + backendRootPath_.string());
    return StartResult::OwnedLaunched;
}

void BackendProcessManager::shutdownOwnedBackend()
{
    if (!ownsBackendProcess_ || ownedBackendProcess_ == nullptr)
    {
        return;
    }

    if (ownedBackendProcess_->isRunning())
    {
        logger_.info("Shutting down owned backend process");
        ownedBackendProcess_->kill();
    }

    ownedBackendProcess_.reset();
    ownsBackendProcess_ = false;
}

bool BackendProcessManager::ownedProcessStillRunning() const noexcept
{
    return ownedBackendProcess_ != nullptr && ownedBackendProcess_->isRunning();
}

std::optional<std::filesystem::path> BackendProcessManager::locateBackendRoot() const
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString();
    const auto executableDir = std::filesystem::path(executable).parent_path();
    const auto cwd = std::filesystem::current_path();
    std::set<std::filesystem::path> seen;
    std::vector<std::filesystem::path> candidates;

    const auto appendCandidate = [&](const std::filesystem::path& candidate)
    {
        if (candidate.empty())
        {
            return;
        }
        std::error_code ec;
        const auto normalized = std::filesystem::weakly_canonical(candidate, ec);
        const auto key = ec ? candidate.lexically_normal() : normalized;
        if (seen.insert(key).second)
        {
            candidates.push_back(key);
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
    std::optional<std::filesystem::path> bestCandidate;
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
        if (std::filesystem::exists(candidate / "run_backend.bat"))
        {
            score += 5;
        }
        const auto installDataBackend = (executableDir / "data" / "backend").lexically_normal();
        const auto parentInstallDataBackend = executableDir.has_parent_path()
            ? (executableDir.parent_path() / "data" / "backend").lexically_normal()
            : std::filesystem::path{};
        if (candidate == installDataBackend)
        {
            score += 500;
        }
        else if (!parentInstallDataBackend.empty() && candidate == parentInstallDataBackend)
        {
            score += 400;
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

    return bestCandidate;
}

std::filesystem::path BackendProcessManager::locatePythonExecutable(const std::filesystem::path& backendRoot) const
{
    const auto venvPython = backendRoot / ".venv" / "Scripts" / "python.exe";
    if (std::filesystem::exists(venvPython))
    {
        return venvPython;
    }

    logger_.warning("Backend venv python was not found; falling back to system python");
    return std::filesystem::path("python");
}

int BackendProcessManager::selectOwnedBackendPort() const
{
    return parsePortFromBackendUrl(probeClient_.backendUrl());
}

std::string BackendProcessManager::buildLaunchCommand(const std::filesystem::path& backendRoot, const std::filesystem::path& pythonExecutable, int port) const
{
    const auto uvicornExecutable = backendRoot / ".venv" / "Scripts" / "uvicorn.exe";
    if (std::filesystem::exists(uvicornExecutable))
    {
        return "\"" + uvicornExecutable.string() + "\" main:app --app-dir \"" + backendRoot.string() + "\" --host 127.0.0.1 --port " + std::to_string(port);
    }

    const auto pythonPart = "\"" + pythonExecutable.string() + "\"";
    return pythonPart + " -m uvicorn main:app --app-dir \"" + backendRoot.string() + "\" --host 127.0.0.1 --port " + std::to_string(port);
}

juce::StringArray BackendProcessManager::buildLaunchArguments(const std::filesystem::path& backendRoot, const std::filesystem::path& pythonExecutable, int port) const
{
    juce::StringArray args;
    const auto uvicornExecutable = backendRoot / ".venv" / "Scripts" / "uvicorn.exe";
    if (std::filesystem::exists(uvicornExecutable))
    {
        args.add(uvicornExecutable.string());
    }
    else
    {
        args.add(pythonExecutable.string());
        args.add("-m");
        args.add("uvicorn");
    }
    args.add("main:app");
    args.add("--app-dir");
    args.add(backendRoot.string());
    args.add("--host");
    args.add("127.0.0.1");
    args.add("--port");
    args.add(std::to_string(port));
    return args;
}
}
