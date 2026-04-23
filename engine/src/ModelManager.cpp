#include "ModelManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace moon::engine
{
namespace
{
struct ParsedUrl
{
    std::string scheme;
    std::string host;
    unsigned short port{0};
    std::string path;
};

std::string trimCopy(std::string value)
{
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
    return value;
}

std::string escape(const std::string& text)
{
    std::string output;
    output.reserve(text.size());
    for (const auto ch : text)
    {
        if (ch == '\\')
        {
            output += "\\\\";
        }
        else if (ch == '"')
        {
            output += "\\\"";
        }
        else
        {
            output += ch;
        }
    }
    return output;
}

std::string extractString(const std::string& content, const std::string& key, const std::string& fallback = {})
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return match[1].str();
    }
    return fallback;
}

std::uint64_t extractUint64(const std::string& content, const std::string& key, std::uint64_t fallback = 0)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return static_cast<std::uint64_t>(std::stoull(match[1].str()));
    }
    return fallback;
}

bool extractBool(const std::string& content, const std::string& key, bool fallback = false)
{
    const std::regex expr("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(content, match, expr) && match.size() > 1)
    {
        return match[1].str() == "true";
    }
    return fallback;
}

std::vector<std::string> extractStringArray(const std::string& content, const std::string& key)
{
    const auto keyPos = content.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
    {
        return {};
    }

    const auto start = content.find('[', keyPos);
    const auto end = content.find(']', start);
    if (start == std::string::npos || end == std::string::npos || end <= start)
    {
        return {};
    }

    const auto segment = content.substr(start + 1, end - start - 1);
    const std::regex expr("\"([^\"]+)\"");
    std::vector<std::string> values;
    for (std::sregex_iterator it(segment.begin(), segment.end(), expr), finish; it != finish; ++it)
    {
        values.push_back((*it)[1].str());
    }
    return values;
}

std::string makeTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::vector<ModelCapability> parseCapabilities(const std::vector<std::string>& values)
{
    std::vector<ModelCapability> capabilities;
    for (const auto& value : values)
    {
        if (value == "song_generation")
        {
            capabilities.push_back(ModelCapability::SongGeneration);
        }
        else if (value == "track_generation")
        {
            capabilities.push_back(ModelCapability::TrackGeneration);
        }
        else if (value == "track_separation")
        {
            capabilities.push_back(ModelCapability::TrackSeparation);
        }
        else if (value == "vocal2bgm")
        {
            capabilities.push_back(ModelCapability::VocalToBgm);
        }
    }
    return capabilities;
}

std::string serializeCapabilities(const std::vector<ModelCapability>& capabilities)
{
    std::ostringstream stream;
    for (std::size_t i = 0; i < capabilities.size(); ++i)
    {
        if (i > 0)
        {
            stream << ", ";
        }
        stream << '"' << modelCapabilityLabel(capabilities[i]) << '"';
    }
    return stream.str();
}

std::string statusStorageValue(ModelStatus status)
{
    switch (status)
    {
    case ModelStatus::NotInstalled: return "not_installed";
    case ModelStatus::Downloading: return "downloading";
    case ModelStatus::Downloaded: return "downloaded";
    case ModelStatus::Verifying: return "verifying";
    case ModelStatus::RuntimeMissing: return "runtime_missing";
    case ModelStatus::RuntimePreparing: return "runtime_preparing";
    case ModelStatus::Ready: return "ready";
    case ModelStatus::Running: return "running";
    case ModelStatus::Broken: return "broken";
    case ModelStatus::Incompatible: return "incompatible";
    case ModelStatus::Failed: return "failed";
    case ModelStatus::UpdateAvailable: return "update_available";
    case ModelStatus::Removing: return "removing";
    }

    return "not_installed";
}

ModelStatus parseStatus(const std::string& text)
{
    if (text == "downloading") return ModelStatus::Downloading;
    if (text == "downloaded") return ModelStatus::Downloaded;
    if (text == "verifying") return ModelStatus::Verifying;
    if (text == "runtime_missing") return ModelStatus::RuntimeMissing;
    if (text == "runtime_preparing") return ModelStatus::RuntimePreparing;
    if (text == "ready") return ModelStatus::Ready;
    if (text == "running") return ModelStatus::Running;
    if (text == "broken") return ModelStatus::Broken;
    if (text == "incompatible") return ModelStatus::Incompatible;
    if (text == "failed") return ModelStatus::Failed;
    if (text == "update_available") return ModelStatus::UpdateAvailable;
    if (text == "removing") return ModelStatus::Removing;
    return ModelStatus::NotInstalled;
}

std::string sanitizedFolderName(const std::string& text)
{
    std::string output;
    output.reserve(text.size());
    for (const auto ch : text)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)))
        {
            output += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        else if (ch == '-' || ch == '_')
        {
            output += ch;
        }
    }
    return output.empty() ? std::string("model") : output;
}

bool copyRecursively(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::error_code ec;
    if (std::filesystem::is_directory(source, ec))
    {
        std::filesystem::create_directories(destination, ec);
        for (const auto& entry : std::filesystem::directory_iterator(source, ec))
        {
            if (ec)
            {
                return false;
            }

            if (!copyRecursively(entry.path(), destination / entry.path().filename()))
            {
                return false;
            }
        }
        return true;
    }

    std::filesystem::create_directories(destination.parent_path(), ec);
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

std::optional<std::string> readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }

    out << content;
    return static_cast<bool>(out);
}

#if defined(_WIN32)
std::optional<ParsedUrl> parseUrl(const std::string& url)
{
    ParsedUrl parsed;
    if (url.rfind("https://", 0) == 0)
    {
        parsed.scheme = "https";
        parsed.port = 443;
    }
    else if (url.rfind("http://", 0) == 0)
    {
        parsed.scheme = "http";
        parsed.port = 80;
    }
    else
    {
        return std::nullopt;
    }

    std::string remaining = url.substr(parsed.scheme == "https" ? 8 : 7);
    auto slashPos = remaining.find('/');
    parsed.host = slashPos == std::string::npos ? remaining : remaining.substr(0, slashPos);
    parsed.path = slashPos == std::string::npos ? "/" : remaining.substr(slashPos);
    if (const auto colonPos = parsed.host.rfind(':'); colonPos != std::string::npos)
    {
        parsed.port = static_cast<unsigned short>(std::clamp(std::stoi(parsed.host.substr(colonPos + 1)), 1, 65535));
        parsed.host = parsed.host.substr(0, colonPos);
    }

    return parsed;
}

bool fetchUrl(const std::string& url,
              std::string* responseBody,
              const std::filesystem::path* destination,
              const std::shared_ptr<moon::engine::ModelOperationState>& operationState = {},
              const std::function<void(std::uint64_t, std::uint64_t)>& progressCallback = {})
{
    const auto parsed = parseUrl(url);
    if (!parsed.has_value())
    {
        return false;
    }

    const std::wstring hostW(parsed->host.begin(), parsed->host.end());
    const std::wstring pathW(parsed->path.begin(), parsed->path.end());

    HINTERNET session = WinHttpOpen(L"MoonAudioEditor/0.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (session == nullptr)
    {
        return false;
    }
    // HF metadata requests are small, but actual model payloads can be large and slow.
    // Keep connection timeouts modest while allowing long receives during downloads.
    WinHttpSetTimeouts(session, 5000, 5000, 30000, 300000);

    HINTERNET connection = WinHttpConnect(session, hostW.c_str(), static_cast<INTERNET_PORT>(parsed->port), 0);
    if (connection == nullptr)
    {
        WinHttpCloseHandle(session);
        return false;
    }

    const DWORD requestFlags = parsed->scheme == "https" ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", pathW.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
    if (request == nullptr)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    const DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, const_cast<DWORD*>(&redirectPolicy), sizeof(redirectPolicy));

    bool success = false;
    std::ofstream out;
    if (destination != nullptr)
    {
        std::error_code ec;
        std::filesystem::create_directories(destination->parent_path(), ec);
        out.open(*destination, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }
    }

    std::string responseBuffer;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(request, nullptr))
    {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 &statusCode,
                                 &statusCodeSize,
                                 WINHTTP_NO_HEADER_INDEX)
            || statusCode < 200
            || statusCode >= 300)
        {
            success = false;
        }
        else
        {
            std::uint64_t contentLength = 0;
            DWORD headerBytes = 0;
            if (!WinHttpQueryHeaders(request,
                                     WINHTTP_QUERY_CONTENT_LENGTH,
                                     WINHTTP_HEADER_NAME_BY_INDEX,
                                     WINHTTP_NO_OUTPUT_BUFFER,
                                     &headerBytes,
                                     WINHTTP_NO_HEADER_INDEX)
                && GetLastError() == ERROR_INSUFFICIENT_BUFFER
                && headerBytes >= sizeof(wchar_t))
            {
                std::wstring contentLengthHeader(headerBytes / sizeof(wchar_t), L'\0');
                if (WinHttpQueryHeaders(request,
                                        WINHTTP_QUERY_CONTENT_LENGTH,
                                        WINHTTP_HEADER_NAME_BY_INDEX,
                                        contentLengthHeader.data(),
                                        &headerBytes,
                                        WINHTTP_NO_HEADER_INDEX))
                {
                    try
                    {
                        contentLength = static_cast<std::uint64_t>(std::stoull(contentLengthHeader.c_str()));
                    }
                    catch (...)
                    {
                        contentLength = 0;
                    }
                }
            }

            DWORD available = 0;
            std::uint64_t totalDownloaded = 0;
            success = true;
            do
            {
                available = 0;
                if (!WinHttpQueryDataAvailable(request, &available))
                {
                    success = false;
                    break;
                }

                if (operationState != nullptr && operationState->isCancelled())
                {
                    success = false;
                    break;
                }

                if (available == 0)
                {
                    break;
                }

                std::string buffer(static_cast<std::size_t>(available), '\0');
                DWORD downloaded = 0;
                if (!WinHttpReadData(request, buffer.data(), available, &downloaded))
                {
                    success = false;
                    break;
                }

                if (operationState != nullptr && operationState->isCancelled())
                {
                    success = false;
                    break;
                }

                if (responseBody != nullptr)
                {
                    responseBuffer.append(buffer.data(), downloaded);
                }

                if (out)
                {
                    out.write(buffer.data(), static_cast<std::streamsize>(downloaded));
                }

                totalDownloaded += static_cast<std::uint64_t>(downloaded);
                if (progressCallback)
                {
                    progressCallback(totalDownloaded, contentLength);
                }
            } while (available > 0);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (responseBody != nullptr && success)
    {
        *responseBody = std::move(responseBuffer);
    }
    return success;
}

bool fetchUrlToString(const std::string& url, std::string& responseBody)
{
    responseBody.clear();
    return fetchUrl(url, &responseBody, nullptr);
}

bool downloadToFile(const std::string& url,
                    const std::filesystem::path& destination,
                    const std::shared_ptr<moon::engine::ModelOperationState>& operationState = {},
                    const std::function<void(std::uint64_t, std::uint64_t)>& progressCallback = {})
{
    return fetchUrl(url, nullptr, &destination, operationState, progressCallback);
}
#endif

std::string percentEncode(std::string_view input, bool encodeSlash)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(input.size() * 3);
    for (const auto ch : input)
    {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || (!encodeSlash && ch == '/'))
        {
            output.push_back(ch);
            continue;
        }

        output.push_back('%');
        output.push_back(kHex[(uch >> 4) & 0x0F]);
        output.push_back(kHex[uch & 0x0F]);
    }
    return output;
}

std::string lowercaseCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string humanizeRepoName(std::string repoId)
{
    if (const auto slashPos = repoId.rfind('/'); slashPos != std::string::npos)
    {
        repoId = repoId.substr(slashPos + 1);
    }

    std::replace(repoId.begin(), repoId.end(), '_', ' ');
    std::replace(repoId.begin(), repoId.end(), '-', ' ');

    bool makeUpper = true;
    for (auto& ch : repoId)
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            makeUpper = true;
            continue;
        }

        ch = makeUpper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))
                       : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        makeUpper = false;
    }
    return repoId;
}

bool containsInsensitive(const std::string& haystack, std::string_view needle)
{
    return lowercaseCopy(haystack).find(lowercaseCopy(std::string(needle))) != std::string::npos;
}

std::string normalizeRemoteModelId(const std::string& repoId)
{
    std::string normalized;
    normalized.reserve(repoId.size());
    for (const auto ch : repoId)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)))
        {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        else if (ch == '-' || ch == '_' || ch == '/' || ch == '.')
        {
            normalized.push_back('-');
        }
    }

    normalized = std::regex_replace(normalized, std::regex("-+"), "-");
    normalized = trimCopy(normalized);
    if (!normalized.empty() && normalized.front() == '-')
    {
        normalized.erase(normalized.begin());
    }
    if (!normalized.empty() && normalized.back() == '-')
    {
        normalized.pop_back();
    }
    return normalized.empty() ? std::string("hf-model") : normalized;
}

std::string inferVersion(const std::string& repoId, const std::string& fallback = "remote")
{
    std::smatch match;
    if (std::regex_search(repoId, match, std::regex("(?:v)?([0-9]+(?:[._][0-9]+)*)", std::regex::icase)) && match.size() > 1)
    {
        auto version = match[1].str();
        std::replace(version.begin(), version.end(), '_', '.');
        if (containsInsensitive(repoId, "xl"))
        {
            version += " XL";
        }
        else if (containsInsensitive(repoId, "turbo"))
        {
            version += " Turbo";
        }
        return version;
    }
    return fallback;
}

std::vector<ModelCapability> inferCapabilitiesFromText(const std::string& text)
{
    const auto lower = lowercaseCopy(text);
    if (lower.find("vocal2bgm") != std::string::npos || (lower.find("vocal") != std::string::npos && lower.find("bgm") != std::string::npos))
    {
        return {ModelCapability::VocalToBgm};
    }
    if (lower.find("separation") != std::string::npos || lower.find("demucs") != std::string::npos || lower.find("stem") != std::string::npos)
    {
        return {ModelCapability::TrackSeparation};
    }
    if (lower.find("song") != std::string::npos || lower.find("ace-step") != std::string::npos || lower.find("acestep") != std::string::npos)
    {
        return {ModelCapability::SongGeneration, ModelCapability::TrackGeneration};
    }
    return {ModelCapability::TrackGeneration};
}

std::string inferDisplayNameFromRepo(const std::string& repoId)
{
    const auto version = inferVersion(repoId, {});
    const auto lower = lowercaseCopy(repoId);

    if (lower.find("vocal2bgm") != std::string::npos)
    {
        return version.empty() ? "ACE-Step Vocal2BGM" : "ACE-Step " + version + " Vocal2BGM";
    }
    if (lower.find("backing") != std::string::npos && lower.find("vocal") != std::string::npos)
    {
        return version.empty() ? "ACE-Step Backing Vocals" : "ACE-Step " + version + " Backing Vocals";
    }
    if (lower.find("vocal") != std::string::npos)
    {
        return version.empty() ? "ACE-Step Vocals" : "ACE-Step " + version + " Vocals";
    }
    if (lower.find("separation") != std::string::npos || lower.find("stem") != std::string::npos)
    {
        return version.empty() ? "ACE-Step Separation" : "ACE-Step " + version + " Separation";
    }
    return version.empty() ? "ACE-Step" : "ACE-Step " + version;
}

std::vector<std::string> extractRepoSiblingPaths(const std::string& content)
{
    std::vector<std::string> siblings;
    const std::regex expr("\"rfilename\"\\s*:\\s*\"([^\"]+)\"");
    for (std::sregex_iterator it(content.begin(), content.end(), expr), finish; it != finish; ++it)
    {
        siblings.push_back((*it)[1].str());
    }
    return siblings;
}

std::map<std::string, std::uint64_t> extractRepoSiblingSizes(const std::string& content)
{
    std::map<std::string, std::uint64_t> sizes;
    const std::regex expr("\\{[^\\{\\}]*\"rfilename\"\\s*:\\s*\"([^\"]+)\"[^\\{\\}]*\"size\"\\s*:\\s*([0-9]+)[^\\{\\}]*\\}");
    for (std::sregex_iterator it(content.begin(), content.end(), expr), finish; it != finish; ++it)
    {
        try
        {
            sizes[(*it)[1].str()] = static_cast<std::uint64_t>(std::stoull((*it)[2].str()));
        }
        catch (...)
        {
        }
    }
    return sizes;
}

std::vector<std::string> extractRepoTreePaths(const std::string& content)
{
    std::vector<std::string> paths;
    const std::regex expr("\"path\"\\s*:\\s*\"([^\"]+)\"");
    for (std::sregex_iterator it(content.begin(), content.end(), expr), finish; it != finish; ++it)
    {
        paths.push_back((*it)[1].str());
    }
    return paths;
}

std::string joinQuotedStrings(const std::vector<std::string>& values)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index > 0)
        {
            stream << ", ";
        }
        stream << '"' << escape(values[index]) << '"';
    }
    return stream.str();
}

bool shouldDownloadRepoFile(const std::string& relativePath)
{
    const auto lower = lowercaseCopy(relativePath);
    const auto filename = std::filesystem::path(lower).filename().string();
    if (filename == ".gitattributes" || filename == "readme.md")
    {
        return false;
    }

    const auto hasSuffix = [&lower](std::string_view suffix)
    {
        return lower.size() >= suffix.size()
            && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix.data(), suffix.size()) == 0;
    };

    return hasSuffix(".json")
        || hasSuffix(".yaml")
        || hasSuffix(".yml")
        || hasSuffix(".txt")
        || hasSuffix(".model")
        || hasSuffix(".safetensors")
        || hasSuffix(".bin")
        || hasSuffix(".pt")
        || hasSuffix(".pth")
        || hasSuffix(".ckpt")
        || hasSuffix(".onnx");
}

std::string humanReadableSize(std::uint64_t bytes)
{
    if (bytes == 0)
    {
        return "0 B";
    }

    static constexpr const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    std::size_t unitIndex = 0;
    while (size >= 1024.0 && unitIndex + 1 < std::size(kUnits))
    {
        size /= 1024.0;
        ++unitIndex;
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 1) << size << ' ' << kUnits[unitIndex];
    return stream.str();
}

bool hasWeightLikeExtension(const std::filesystem::path& path)
{
    const auto extension = lowercaseCopy(path.extension().string());
    return extension == ".safetensors"
        || extension == ".bin"
        || extension == ".ckpt"
        || extension == ".pt"
        || extension == ".pth"
        || extension == ".onnx";
}

std::string readFilePrefix(const std::filesystem::path& path, std::size_t maxBytes = 1024)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return {};
    }

    std::string prefix(maxBytes, '\0');
    in.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    prefix.resize(static_cast<std::size_t>(in.gcount()));
    return prefix;
}

bool looksLikeHtmlOrLfsPointer(const std::string& text)
{
    const auto lower = lowercaseCopy(text);
    return lower.find("<html") != std::string::npos
        || lower.find("<!doctype html") != std::string::npos
        || lower.find("git-lfs.github.com/spec/v1") != std::string::npos
        || lower.find("<body") != std::string::npos
        || lower.find("access denied") != std::string::npos
        || lower.find("repository not found") != std::string::npos;
}

bool looksLikeValidWeightFile(const std::filesystem::path& path, std::string* errorMessage = nullptr)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Missing model file: " + path.filename().string();
        }
        return false;
    }

    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < 256 * 1024)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Model file is too small to be valid: " + path.filename().string();
        }
        return false;
    }

    const auto prefix = readFilePrefix(path);
    if (looksLikeHtmlOrLfsPointer(prefix))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Downloaded file is not a real model payload: " + path.filename().string();
        }
        return false;
    }

    return true;
}

std::vector<std::string> collectModelFilesForManifest(const std::filesystem::path& root)
{
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
    {
        if (ec || !entry.is_regular_file())
        {
            continue;
        }

        if (entry.path().filename() == "moon_model_install.json")
        {
            continue;
        }

        const auto extension = lowercaseCopy(entry.path().extension().string());
        const auto filename = lowercaseCopy(entry.path().filename().string());
        const bool modelRelevant = hasWeightLikeExtension(entry.path())
            || filename == "config.json"
            || extension == ".json"
            || extension == ".yaml"
            || extension == ".yml"
            || extension == ".txt";
        if (!modelRelevant)
        {
            continue;
        }

        const auto relative = std::filesystem::relative(entry.path(), root, ec);
        files.push_back(ec ? entry.path().filename().generic_string() : relative.generic_string());
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::optional<ModelDescriptor> inferCatalogDescriptorForLocalFolder(
    const std::vector<ModelDescriptor>& catalog,
    const std::filesystem::path& folderPath)
{
    if (catalog.empty())
    {
        return std::nullopt;
    }

    const auto folderName = folderPath.filename().string();
    const auto folderKey = sanitizedFolderName(folderName);
    const auto folderLower = lowercaseCopy(folderName);

    int bestScore = 0;
    const ModelDescriptor* best = nullptr;
    const ModelDescriptor* aceFallback = nullptr;
    for (const auto& descriptor : catalog)
    {
        const auto idKey = sanitizedFolderName(descriptor.id);
        const auto displayKey = sanitizedFolderName(descriptor.displayName);
        const auto remoteKey = sanitizedFolderName(descriptor.remoteId);

        int score = 0;
        if (!idKey.empty() && (folderKey.find(idKey) != std::string::npos || idKey.find(folderKey) != std::string::npos))
        {
            score += 80;
        }
        if (!remoteKey.empty() && (folderKey.find(remoteKey) != std::string::npos || remoteKey.find(folderKey) != std::string::npos))
        {
            score += 70;
        }
        if (!displayKey.empty() && (folderKey.find(displayKey) != std::string::npos || displayKey.find(folderKey) != std::string::npos))
        {
            score += 50;
        }
        if (!descriptor.version.empty() && containsInsensitive(folderName, descriptor.version))
        {
            score += 20;
        }
        if (containsInsensitive(folderName, "xl") && containsInsensitive(descriptor.displayName + descriptor.id + descriptor.remoteId, "xl"))
        {
            score += 15;
        }
        if (containsInsensitive(folderName, "turbo") && containsInsensitive(descriptor.displayName + descriptor.id + descriptor.remoteId, "turbo"))
        {
            score += 15;
        }

        if (score > bestScore)
        {
            bestScore = score;
            best = &descriptor;
        }

        if (aceFallback == nullptr
            && (descriptor.id == "ace-step"
                || containsInsensitive(descriptor.id, "ace-step")
                || containsInsensitive(descriptor.id, "acestep")))
        {
            aceFallback = &descriptor;
        }
    }

    if (best != nullptr && bestScore >= 50)
    {
        return *best;
    }

    if ((folderLower.find("ace") != std::string::npos || folderLower.find("acestep") != std::string::npos)
        && aceFallback != nullptr)
    {
        return *aceFallback;
    }

    return std::nullopt;
}

std::vector<ModelDescriptor> parseHuggingFaceCatalog(const std::string& content)
{
    std::vector<ModelDescriptor> descriptors;
    const std::regex repoExpr("\"id\"\\s*:\\s*\"([^\"]+)\"");
    for (std::sregex_iterator it(content.begin(), content.end(), repoExpr), finish; it != finish; ++it)
    {
        const auto repoId = (*it)[1].str();
        const auto lowerRepoId = lowercaseCopy(repoId);
        if (lowerRepoId.find("ace-step") == std::string::npos && lowerRepoId.find("acestep") == std::string::npos)
        {
            continue;
        }

        const auto matchOffset = static_cast<std::size_t>((*it).position());
        const auto trailingSegment = content.substr(matchOffset, std::min<std::size_t>(768, content.size() - matchOffset));

        ModelDescriptor descriptor;
        descriptor.id = normalizeRemoteModelId(repoId);
        descriptor.displayName = inferDisplayNameFromRepo(repoId);
        descriptor.version = inferVersion(repoId);
        descriptor.source = "huggingface";
        descriptor.provider = "Hugging Face";
        descriptor.description = "Discovered from Hugging Face ACE-Step catalog sync.";
        descriptor.downloadUri = "hf://" + repoId;
        descriptor.remoteId = repoId;
        descriptor.homepageUrl = "https://huggingface.co/" + repoId;
        descriptor.lastModified = extractString(trailingSegment, "lastModified");
        descriptor.capabilities = inferCapabilitiesFromText(repoId);
        descriptors.push_back(std::move(descriptor));
    }

    std::sort(descriptors.begin(), descriptors.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.displayName < rhs.displayName;
    });
    descriptors.erase(std::unique(descriptors.begin(), descriptors.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.id == rhs.id;
    }), descriptors.end());
    return descriptors;
}

std::vector<ModelDescriptor> defaultCatalog()
{
    return {
        ModelDescriptor{
            "ace-step",
            "ACE-Step",
            "1.5",
            "catalog",
            "ACE-Step",
            "Primary ACE-Step model family for song and track generation.",
            {},
            {},
            {},
            {},
            0,
            true,
            {ModelCapability::SongGeneration, ModelCapability::TrackGeneration}},
        ModelDescriptor{
            "ace-step-vocal2bgm",
            "ACE-Step Vocal2BGM",
            "1.0",
            "catalog",
            "ACE-Step",
            "ACE-Step variant focused on vocal-to-bgm style tasks.",
            {},
            {},
            {},
            {},
            0,
            true,
            {ModelCapability::VocalToBgm}}
    };
}
}

ModelManager::ModelManager(Logger& logger, std::filesystem::path rootPath)
    : logger_(logger)
    , rootPath_(std::move(rootPath))
    , catalogDirectory_(rootPath_ / "catalog")
    , installsDirectory_(rootPath_ / "installs")
    , cacheDirectory_(rootPath_ / "cache")
    , manifestsDirectory_(rootPath_ / "manifests")
    , registryPath_(manifestsDirectory_ / "installed_models.json")
{
    refresh();
}

bool ModelManager::refresh(std::string* errorMessage)
{
    ensureDirectories();
    seedCatalogIfNeeded();
    if (!loadCatalog(errorMessage))
    {
        return false;
    }

    if (!loadRegistry(errorMessage))
    {
        return false;
    }

    reconcileInstalledStatuses();
    return saveRegistry(errorMessage);
}

bool ModelManager::syncRemoteCatalog(std::string* errorMessage)
{
    return syncHuggingFaceCatalog(errorMessage);
}

bool ModelManager::pollOperations(std::string* errorMessage)
{
    bool changed = false;
    std::vector<std::string> completedIds;

    {
        std::lock_guard<std::mutex> lock(operationsMutex_);
        for (auto& [modelId, future] : operationFutures_)
        {
            if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                continue;
            }

            auto result = future.get();
            completedIds.push_back(modelId);
            changed = true;

            if (result.success && result.installedRecord.has_value())
            {
                installed_[modelId] = *result.installedRecord;
                logger_.info("Model operation completed for " + modelId);
            }
            else if (result.cancelled)
            {
                logger_.info("Model operation cancelled for " + modelId);
            }
            else
            {
                logger_.warning("Model operation failed for " + modelId + ": " + result.errorMessage);
                if (errorMessage != nullptr && errorMessage->empty())
                {
                    *errorMessage = result.errorMessage;
                }
            }
        }

        for (const auto& modelId : completedIds)
        {
            operationFutures_.erase(modelId);
            const auto installedIt = installed_.find(modelId);
            const auto operationIt = operations_.find(modelId);
            const bool keepFailedState = operationIt != operations_.end()
                && operationIt->second != nullptr
                && !operationIt->second->message.empty()
                && operationIt->second->message != "Cancelled"
                && installedIt == installed_.end();

            if (!keepFailedState)
            {
                operations_.erase(modelId);
            }
        }
    }

    if (changed)
    {
        reconcileInstalledStatuses();
        saveRegistry(errorMessage);
    }
    return changed;
}

ModelRegistrySnapshot ModelManager::snapshot() const
{
    ModelRegistrySnapshot snapshot;
    snapshot.available = catalog_;
    snapshot.activeBindings = activeBindings_;
    snapshot.localFolders = scanLocalFolders();
    for (const auto& [_, installed] : installed_)
    {
        auto item = installed;
        item.selectedForGeneration = std::any_of(
            activeBindings_.begin(),
            activeBindings_.end(),
            [&item](const auto& binding)
            {
                return binding.second == item.id;
            });
        snapshot.installed.push_back(item);
    }

    std::sort(snapshot.installed.begin(), snapshot.installed.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.displayName < rhs.displayName;
    });
    std::sort(snapshot.available.begin(), snapshot.available.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.displayName < rhs.displayName;
    });

    std::lock_guard<std::mutex> lock(operationsMutex_);
    for (auto& item : snapshot.installed)
    {
        const auto opIt = operations_.find(item.id);
        if (opIt == operations_.end() || opIt->second == nullptr)
        {
            continue;
        }

        std::lock_guard<std::mutex> opLock(opIt->second->mutex);
        item.status = opIt->second->status;
        item.operationProgress = opIt->second->progress;
        item.operationStatusText = opIt->second->message;
    }
    for (auto& item : snapshot.available)
    {
        const auto opIt = operations_.find(item.id);
        if (opIt == operations_.end() || opIt->second == nullptr)
        {
            continue;
        }

        std::lock_guard<std::mutex> opLock(opIt->second->mutex);
        item.operationProgress = opIt->second->progress;
        item.operationStatusText = opIt->second->message;
    }
    return snapshot;
}

std::vector<ModelDescriptor> ModelManager::modelsForCapability(ModelCapability capability) const
{
    std::vector<ModelDescriptor> models;
    for (const auto& descriptor : catalog_)
    {
        if (std::find(descriptor.capabilities.begin(), descriptor.capabilities.end(), capability) != descriptor.capabilities.end())
        {
            models.push_back(descriptor);
        }
    }
    return models;
}

std::optional<ResolvedModelInfo> ModelManager::resolveActiveModel(ModelCapability capability) const
{
    const auto binding = activeBindings_.find(capability);
    if (binding != activeBindings_.end())
    {
        if (auto resolved = resolveInstalledModel(binding->second); resolved.has_value())
        {
            return resolved;
        }
    }

    for (const auto& [_, installed] : installed_)
    {
        if ((installed.status == ModelStatus::Ready || installed.status == ModelStatus::UpdateAvailable)
            && std::find(installed.capabilities.begin(), installed.capabilities.end(), capability) != installed.capabilities.end())
        {
            return ResolvedModelInfo{
                installed.id,
                installed.displayName,
                installed.version,
                installed.installPath,
                installed.capabilities,
                installed.status};
        }
    }

    return std::nullopt;
}

std::optional<ResolvedModelInfo> ModelManager::resolveInstalledModel(const std::string& modelId) const
{
    const auto it = installed_.find(modelId);
    if (it == installed_.end())
    {
        return std::nullopt;
    }

    const auto& installed = it->second;
    if (installed.status != ModelStatus::Ready && installed.status != ModelStatus::UpdateAvailable)
    {
        return std::nullopt;
    }

    return ResolvedModelInfo{
        installed.id,
        installed.displayName,
        installed.version,
        installed.installPath,
        installed.capabilities,
        installed.status};
}

bool ModelManager::setActiveModel(ModelCapability capability, const std::string& modelId, std::string& errorMessage)
{
    const auto resolved = resolveInstalledModel(modelId);
    if (!resolved.has_value())
    {
        errorMessage = "Model is not installed or not ready";
        return false;
    }

    if (std::find(resolved->capabilities.begin(), resolved->capabilities.end(), capability) == resolved->capabilities.end())
    {
        errorMessage = "Selected model does not support this generation capability";
        return false;
    }

    activeBindings_[capability] = modelId;
    logger_.info("Set active model " + modelId + " for " + std::string(modelCapabilityLabel(capability)));
    return saveRegistry(&errorMessage);
}

bool ModelManager::addExistingModelFolder(const std::string& modelId, const std::filesystem::path& folderPath, std::string& errorMessage)
{
    const auto descriptor = findCatalogModel(modelId);
    if (!descriptor.has_value())
    {
        errorMessage = "Unknown model id";
        return false;
    }

    if (!validateModelFolder(folderPath, errorMessage))
    {
        return false;
    }

    auto installed = makeInstalledRecord(*descriptor, folderPath, "existing-folder", ModelStatus::Ready);
    installed.installedAt = makeTimestamp();
    installed_[modelId] = installed;
    ModelInstallManifest manifest;
    manifest.modelId = descriptor->id;
    manifest.remoteId = descriptor->remoteId;
    manifest.version = descriptor->version;
    manifest.installPath = folderPath;
    saveInstallManifest(manifest, nullptr);
    logger_.info("Registered existing model folder for " + modelId + " at " + folderPath.string());
    return saveRegistry(&errorMessage);
}

bool ModelManager::verifyModel(const std::string& modelId, std::string& errorMessage)
{
    const auto it = installed_.find(modelId);
    if (it == installed_.end())
    {
        errorMessage = "Model is not installed";
        return false;
    }

    if (validateModelFolder(it->second.installPath, errorMessage))
    {
        it->second.status = ModelStatus::Ready;
        ModelInstallManifest manifest;
        if (loadInstallManifest(it->second.installPath, manifest) && !manifest.modelId.empty() && manifest.modelId != modelId)
        {
            it->second.status = ModelStatus::Broken;
            errorMessage = "Install manifest does not match registered model id";
        }
        logger_.info("Verified model " + modelId);
    }
    else
    {
        it->second.status = ModelStatus::Broken;
        logger_.warning("Verification failed for model " + modelId + ": " + errorMessage);
    }

    return saveRegistry(&errorMessage);
}

bool ModelManager::removeModel(const std::string& modelId, std::string& errorMessage)
{
    const auto it = installed_.find(modelId);
    if (it == installed_.end())
    {
        errorMessage = "Model is not installed";
        return false;
    }

    std::error_code ec;
    const auto normalizedInstallPath = it->second.installPath.lexically_normal();
    const auto normalizedInstallsRoot = installsDirectory_.lexically_normal();
    const bool ownedInstallPath = normalizedInstallPath.string().rfind(normalizedInstallsRoot.string(), 0) == 0;
    if (ownedInstallPath)
    {
        std::filesystem::remove_all(it->second.installPath, ec);
    }
    installed_.erase(it);
    for (auto binding = activeBindings_.begin(); binding != activeBindings_.end();)
    {
        if (binding->second == modelId)
        {
            binding = activeBindings_.erase(binding);
        }
        else
        {
            ++binding;
        }
    }

    logger_.info("Removed model " + modelId);
    return saveRegistry(&errorMessage);
}

bool ModelManager::downloadModel(const std::string& modelId, std::string& errorMessage)
{
    const auto descriptor = findCatalogModel(modelId);
    if (!descriptor.has_value())
    {
        errorMessage = "Unknown model id";
        return false;
    }

    if (descriptor->downloadUri.empty())
    {
        errorMessage = "No download source configured for this model. Use Add Existing Folder.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(operationsMutex_);
        if (operationFutures_.find(modelId) != operationFutures_.end())
        {
            errorMessage = "Model download is already in progress";
            return false;
        }
    }

    auto operationState = std::make_shared<ModelOperationState>();
    operationState->setProgress(ModelStatus::Downloading, 0.0, "Queued download");
    {
        std::lock_guard<std::mutex> lock(operationsMutex_);
        operations_[modelId] = operationState;
        operationFutures_[modelId] = std::async(std::launch::async, [this, descriptor = *descriptor, operationState]()
        {
            return performDownloadOperation(descriptor, operationState);
        });
    }

    logger_.info("Started background model download for " + modelId);
    return true;
}

bool ModelManager::updateModel(const std::string& modelId, std::string& errorMessage)
{
    return downloadModel(modelId, errorMessage);
}

bool ModelManager::cancelModelOperation(const std::string& modelId, std::string& errorMessage)
{
    std::lock_guard<std::mutex> lock(operationsMutex_);
    const auto it = operations_.find(modelId);
    if (it == operations_.end() || it->second == nullptr)
    {
        errorMessage = "No active model operation";
        return false;
    }

    it->second->requestCancel();
    logger_.info("Cancellation requested for model operation " + modelId);
    return true;
}

bool ModelManager::cancelAllModelOperations(std::string* errorMessage)
{
    std::lock_guard<std::mutex> lock(operationsMutex_);
    if (operations_.empty())
    {
        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return false;
    }

    for (auto& [modelId, operation] : operations_)
    {
        if (operation != nullptr)
        {
            operation->requestCancel();
            logger_.info("Cancellation requested for model operation " + modelId);
        }
    }

    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

void ModelManager::ensureDirectories() const
{
    std::filesystem::create_directories(catalogDirectory_);
    std::filesystem::create_directories(installsDirectory_);
    std::filesystem::create_directories(cacheDirectory_);
    std::filesystem::create_directories(manifestsDirectory_);
}

void ModelManager::seedCatalogIfNeeded()
{
    if (std::filesystem::exists(catalogDirectory_ / "ace-step.json"))
    {
        return;
    }

    for (const auto& descriptor : defaultCatalog())
    {
        std::ofstream out(catalogDirectory_ / (descriptor.id + ".json"), std::ios::trunc);
        if (!out)
        {
            continue;
        }

        out << "{\n";
        out << "  \"id\": \"" << escape(descriptor.id) << "\",\n";
        out << "  \"display_name\": \"" << escape(descriptor.displayName) << "\",\n";
        out << "  \"version\": \"" << escape(descriptor.version) << "\",\n";
        out << "  \"source\": \"" << escape(descriptor.source) << "\",\n";
        out << "  \"provider\": \"" << escape(descriptor.provider) << "\",\n";
        out << "  \"description\": \"" << escape(descriptor.description) << "\",\n";
        out << "  \"download_uri\": \"" << escape(descriptor.downloadUri) << "\",\n";
        out << "  \"approximate_size_mb\": " << descriptor.approximateSizeMb << ",\n";
        out << "  \"supports_existing_folder\": " << (descriptor.supportsExistingFolder ? "true" : "false") << ",\n";
        out << "  \"capabilities\": [" << serializeCapabilities(descriptor.capabilities) << "]\n";
        out << "}\n";
    }
}

bool ModelManager::loadCatalog(std::string* errorMessage)
{
    catalog_.clear();

    std::error_code ec;
    if (!std::filesystem::exists(catalogDirectory_, ec))
    {
        return true;
    }

    for (const auto& entry : std::filesystem::directory_iterator(catalogDirectory_, ec))
    {
        if (ec)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Could not enumerate model catalog";
            }
            return false;
        }

        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }

        std::ifstream in(entry.path());
        if (!in)
        {
            continue;
        }

        std::stringstream buffer;
        buffer << in.rdbuf();
        const auto content = buffer.str();

        ModelDescriptor descriptor;
        descriptor.id = extractString(content, "id");
        descriptor.displayName = extractString(content, "display_name", descriptor.id);
        descriptor.version = extractString(content, "version", "unknown");
        descriptor.source = extractString(content, "source", "catalog");
        descriptor.provider = extractString(content, "provider", descriptor.displayName);
        descriptor.description = extractString(content, "description");
        descriptor.downloadUri = extractString(content, "download_uri");
        descriptor.remoteId = extractString(content, "remote_id");
        descriptor.homepageUrl = extractString(content, "homepage_url");
        descriptor.lastModified = extractString(content, "last_modified");
        descriptor.approximateSizeMb = extractUint64(content, "approximate_size_mb", 0);
        descriptor.supportsExistingFolder = extractBool(content, "supports_existing_folder", true);
        descriptor.capabilities = parseCapabilities(extractStringArray(content, "capabilities"));
        if (!descriptor.id.empty())
        {
            catalog_.push_back(descriptor);
        }
    }

    return true;
}

bool ModelManager::loadRegistry(std::string* errorMessage)
{
    installed_.clear();
    activeBindings_.clear();

    if (!std::filesystem::exists(registryPath_))
    {
        return true;
    }

    std::ifstream in(registryPath_);
    if (!in)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Could not read model registry";
        }
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto content = buffer.str();

    const auto installedStart = content.find("\"installed\"");
    if (installedStart != std::string::npos)
    {
        const auto arrayStart = content.find('[', installedStart);
        const auto arrayEnd = content.find(']', arrayStart);
        if (arrayStart != std::string::npos && arrayEnd != std::string::npos)
        {
            const auto installedSegment = content.substr(arrayStart, arrayEnd - arrayStart + 1);
            const std::regex modelExpr("\\{\"id\"\\s*:\\s*\"([^\"]+)\",\\s*\"display_name\"\\s*:\\s*\"([^\"]*)\",\\s*\"version\"\\s*:\\s*\"([^\"]*)\",\\s*\"install_path\"\\s*:\\s*\"([^\"]*)\",\\s*\"source\"\\s*:\\s*\"([^\"]*)\",\\s*\"status\"\\s*:\\s*\"([^\"]*)\",\\s*\"installed_at\"\\s*:\\s*\"([^\"]*)\",\\s*\"capabilities\"\\s*:\\s*\\[([^\\]]*)\\]\\}");
            for (std::sregex_iterator it(installedSegment.begin(), installedSegment.end(), modelExpr), finish; it != finish; ++it)
            {
                InstalledModelInfo info;
                info.id = (*it)[1].str();
                info.displayName = (*it)[2].str();
                info.version = (*it)[3].str();
                info.installPath = (*it)[4].str();
                info.source = (*it)[5].str();
                info.status = parseStatus((*it)[6].str());
                info.installedAt = (*it)[7].str();
                info.capabilities = parseCapabilities(extractStringArray(std::string("{\"capabilities\":[" + (*it)[8].str() + "]}"), "capabilities"));
                installed_.emplace(info.id, info);
            }
        }
    }

    const auto bindingsStart = content.find("\"active_bindings\"");
    if (bindingsStart != std::string::npos)
    {
        const auto objectStart = content.find('{', bindingsStart);
        const auto objectEnd = content.find('}', objectStart);
        if (objectStart != std::string::npos && objectEnd != std::string::npos)
        {
            const auto segment = content.substr(objectStart, objectEnd - objectStart + 1);
            for (const auto capability : {ModelCapability::SongGeneration, ModelCapability::TrackGeneration, ModelCapability::TrackSeparation, ModelCapability::VocalToBgm})
            {
                const auto boundModelId = extractString(segment, std::string(modelCapabilityLabel(capability)));
                if (!boundModelId.empty())
                {
                    activeBindings_[capability] = boundModelId;
                }
            }
        }
    }

    return true;
}

bool ModelManager::saveRegistry(std::string* errorMessage) const
{
    std::ofstream out(registryPath_, std::ios::trunc);
    if (!out)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Could not write model registry";
        }
        return false;
    }

    out << "{\n";
    out << "  \"installed\": [\n";
    bool first = true;
    for (const auto& [_, installed] : installed_)
    {
        if (!first)
        {
            out << ",\n";
        }
        out << "    {\"id\": \"" << escape(installed.id)
            << "\", \"display_name\": \"" << escape(installed.displayName)
            << "\", \"version\": \"" << escape(installed.version)
            << "\", \"install_path\": \"" << escape(installed.installPath.string())
            << "\", \"source\": \"" << escape(installed.source)
            << "\", \"status\": \"" << escape(statusStorageValue(installed.status))
            << "\", \"installed_at\": \"" << escape(installed.installedAt)
            << "\", \"capabilities\": [" << serializeCapabilities(installed.capabilities) << "]}";
        first = false;
    }
    out << "\n  ],\n";
    out << "  \"active_bindings\": {\n";
    bool firstBinding = true;
    for (const auto capability : {ModelCapability::SongGeneration, ModelCapability::TrackGeneration, ModelCapability::TrackSeparation, ModelCapability::VocalToBgm})
    {
        const auto it = activeBindings_.find(capability);
        if (!firstBinding)
        {
            out << ",\n";
        }
        out << "    \"" << modelCapabilityLabel(capability) << "\": \"" << escape(it == activeBindings_.end() ? std::string{} : it->second) << '"';
        firstBinding = false;
    }
    out << "\n  }\n";
    out << "}\n";
    return true;
}

bool ModelManager::syncHuggingFaceCatalog(std::string* errorMessage)
{
#if !defined(_WIN32)
    if (errorMessage != nullptr)
    {
        *errorMessage = "Hugging Face catalog sync is currently implemented on Windows only";
    }
    return false;
#else
    const auto cachePath = cacheDirectory_ / "huggingface_ace_step_catalog.json";
    constexpr auto kCatalogTtl = std::chrono::hours(6);

    std::string responseBody;
    bool loadedFromCache = false;
    std::error_code ec;
    if (std::filesystem::exists(cachePath, ec))
    {
        const auto now = std::filesystem::file_time_type::clock::now();
        const auto modifiedAt = std::filesystem::last_write_time(cachePath, ec);
        if (!ec && now - modifiedAt <= kCatalogTtl)
        {
            if (const auto cached = readTextFile(cachePath); cached.has_value())
            {
                responseBody = *cached;
                loadedFromCache = true;
            }
        }
    }

    if (responseBody.empty() && fetchUrlToString("https://huggingface.co/api/models?search=ace-step&limit=100&full=true", responseBody))
    {
        writeTextFile(cachePath, responseBody);
    }

    if (responseBody.empty())
    {
        if (const auto cached = readTextFile(cachePath); cached.has_value())
        {
            responseBody = *cached;
            loadedFromCache = true;
        }
    }

    if (responseBody.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Could not refresh Hugging Face model catalog";
        }
        return false;
    }

    const auto descriptors = parseHuggingFaceCatalog(responseBody);
    if (descriptors.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Hugging Face catalog sync returned no ACE-Step models";
        }
        return false;
    }

    for (const auto& descriptor : descriptors)
    {
        std::string writeError;
        if (!writeCatalogDescriptor(descriptor, &writeError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = writeError;
            }
            return false;
        }
    }

    logger_.info(std::string(loadedFromCache ? "Loaded " : "Synced ")
                 + std::to_string(descriptors.size())
                 + " ACE-Step model entries "
                 + (loadedFromCache ? "from cached Hugging Face catalog" : "from Hugging Face"));
    return true;
#endif
}

std::vector<LocalModelFolderInfo> ModelManager::scanLocalFolders() const
{
    std::vector<LocalModelFolderInfo> folders;
    std::error_code ec;
    if (!std::filesystem::exists(installsDirectory_, ec))
    {
        return folders;
    }

    for (const auto& entry : std::filesystem::directory_iterator(installsDirectory_, ec))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        LocalModelFolderInfo info;
        info.path = entry.path();
        info.detectedName = entry.path().filename().string();
        std::string validationError;
        info.valid = validateModelFolder(entry.path(), validationError);
        info.statusNote = info.valid ? "Usable folder" : validationError;
        folders.push_back(info);
    }
    return folders;
}

bool ModelManager::validateModelFolder(const std::filesystem::path& folderPath, std::string& errorMessage) const
{
    std::error_code ec;
    if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec))
    {
        errorMessage = "Folder does not exist";
        return false;
    }

    bool foundValidWeights = false;
    bool foundConfig = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath, ec))
    {
        if (ec)
        {
            errorMessage = "Could not read model folder";
            return false;
        }

        if (!entry.is_regular_file())
        {
            continue;
        }

        if (entry.path().filename() == "config.json")
        {
            foundConfig = true;
        }

        if (hasWeightLikeExtension(entry.path()))
        {
            std::string weightError;
            if (looksLikeValidWeightFile(entry.path(), &weightError))
            {
                foundValidWeights = true;
            }
        }
    }

    ModelInstallManifest manifest;
    if (loadInstallManifest(folderPath, manifest) && !manifest.files.empty())
    {
        for (const auto& relativeFile : manifest.files)
        {
            const auto candidate = folderPath / std::filesystem::path(relativeFile);
            if (!std::filesystem::exists(candidate, ec))
            {
                errorMessage = "Install manifest references a missing file: " + relativeFile;
                return false;
            }

            if (hasWeightLikeExtension(candidate))
            {
                std::string weightError;
                if (!looksLikeValidWeightFile(candidate, &weightError))
                {
                    errorMessage = weightError;
                    return false;
                }
                foundValidWeights = true;
            }
        }
    }

    if (!foundValidWeights)
    {
        errorMessage = foundConfig
            ? "Only config files were found, but no real model weights were installed"
            : "No valid model weights found";
        return false;
    }

    errorMessage.clear();
    return true;
}

std::optional<ModelDescriptor> ModelManager::findCatalogModel(const std::string& modelId) const
{
    const auto it = std::find_if(catalog_.begin(), catalog_.end(), [&modelId](const auto& descriptor)
    {
        return descriptor.id == modelId;
    });
    if (it == catalog_.end())
    {
        return std::nullopt;
    }
    return *it;
}

bool ModelManager::writeCatalogDescriptor(const ModelDescriptor& descriptor, std::string* errorMessage) const
{
    std::ofstream out(catalogPathForModelId(descriptor.id), std::ios::trunc);
    if (!out)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Could not write model descriptor for " + descriptor.id;
        }
        return false;
    }

    out << "{\n";
    out << "  \"id\": \"" << escape(descriptor.id) << "\",\n";
    out << "  \"display_name\": \"" << escape(descriptor.displayName) << "\",\n";
    out << "  \"version\": \"" << escape(descriptor.version) << "\",\n";
    out << "  \"source\": \"" << escape(descriptor.source) << "\",\n";
    out << "  \"provider\": \"" << escape(descriptor.provider) << "\",\n";
    out << "  \"description\": \"" << escape(descriptor.description) << "\",\n";
    out << "  \"download_uri\": \"" << escape(descriptor.downloadUri) << "\",\n";
    out << "  \"remote_id\": \"" << escape(descriptor.remoteId) << "\",\n";
    out << "  \"homepage_url\": \"" << escape(descriptor.homepageUrl) << "\",\n";
    out << "  \"last_modified\": \"" << escape(descriptor.lastModified) << "\",\n";
    out << "  \"approximate_size_mb\": " << descriptor.approximateSizeMb << ",\n";
    out << "  \"supports_existing_folder\": " << (descriptor.supportsExistingFolder ? "true" : "false") << ",\n";
    out << "  \"capabilities\": [" << serializeCapabilities(descriptor.capabilities) << "]\n";
    out << "}\n";
    return true;
}

std::filesystem::path ModelManager::catalogPathForModelId(const std::string& modelId) const
{
    return catalogDirectory_ / (sanitizedFolderName(modelId) + ".json");
}

std::filesystem::path ModelManager::installManifestPath(const std::filesystem::path& installPath) const
{
    return installPath / "moon_model_install.json";
}

bool ModelManager::loadInstallManifest(const std::filesystem::path& installPath, ModelInstallManifest& manifest) const
{
    manifest = {};
    const auto content = readTextFile(installManifestPath(installPath));
    if (!content.has_value())
    {
        return false;
    }

    manifest.modelId = extractString(*content, "model_id");
    manifest.remoteId = extractString(*content, "remote_id");
    manifest.version = extractString(*content, "version");
    manifest.installPath = installPath;
    manifest.files = extractStringArray(*content, "files");
    return true;
}

bool ModelManager::saveInstallManifest(const ModelInstallManifest& manifest, std::string* errorMessage) const
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"model_id\": \"" << escape(manifest.modelId) << "\",\n";
    out << "  \"remote_id\": \"" << escape(manifest.remoteId) << "\",\n";
    out << "  \"version\": \"" << escape(manifest.version) << "\",\n";
    out << "  \"files\": [" << joinQuotedStrings(manifest.files) << "]\n";
    out << "}\n";

    if (!writeTextFile(installManifestPath(manifest.installPath), out.str()))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Could not write model install manifest";
        }
        return false;
    }

    return true;
}

ModelOperationResult ModelManager::performDownloadOperation(ModelDescriptor descriptor, std::shared_ptr<ModelOperationState> operationState) const
{
    ModelOperationResult result;
    result.modelId = descriptor.id;

    const auto installPath = installsDirectory_ / (sanitizedFolderName(descriptor.id) + "-" + sanitizedFolderName(descriptor.version));
    std::error_code ec;
    std::filesystem::remove_all(installPath, ec);
    std::filesystem::create_directories(installPath, ec);

    std::vector<std::string> downloadedFiles;
    bool success = false;
    std::string errorMessage;

    const auto handleCancelled = [&]() -> ModelOperationResult
    {
        std::filesystem::remove_all(installPath, ec);
        operationState->setProgress(ModelStatus::NotInstalled, 0.0, "Cancelled");
        result.cancelled = true;
        result.errorMessage = "Model download cancelled";
        return result;
    };

    if (operationState->isCancelled())
    {
        return handleCancelled();
    }

    if (descriptor.downloadUri.rfind("file://", 0) == 0)
    {
        operationState->setProgress(ModelStatus::Downloading, 0.10, "Copying local model files");
        const auto localPath = std::filesystem::path(descriptor.downloadUri.substr(7));
        success = copyRecursively(localPath, installPath / localPath.filename());
    }
    else if (std::filesystem::exists(descriptor.downloadUri))
    {
        operationState->setProgress(ModelStatus::Downloading, 0.10, "Copying model files");
        const auto localPath = std::filesystem::path(descriptor.downloadUri);
        success = copyRecursively(localPath, installPath / localPath.filename());
    }
    else if (descriptor.downloadUri.rfind("hf://", 0) == 0)
    {
#if defined(_WIN32)
        const auto remoteId = descriptor.remoteId.empty() ? descriptor.downloadUri.substr(5) : descriptor.remoteId;
        operationState->setProgress(ModelStatus::Downloading, 0.01, "Fetching Hugging Face file list");
        const auto metadataUrl = "https://huggingface.co/api/models/" + percentEncode(remoteId, false);
        std::string metadata;
        success = fetchUrlToString(metadataUrl, metadata);
        if (success)
        {
            const auto siblingSizes = extractRepoSiblingSizes(metadata);
            auto candidateFiles = extractRepoSiblingPaths(metadata);
            if (candidateFiles.empty())
            {
                std::string treeMetadata;
                operationState->setProgress(ModelStatus::Downloading, 0.03, "Resolving repository file tree");
                const auto treeUrl = "https://huggingface.co/api/models/" + percentEncode(remoteId, false) + "/tree/main?recursive=true";
                if (fetchUrlToString(treeUrl, treeMetadata))
                {
                    candidateFiles = extractRepoTreePaths(treeMetadata);
                }
            }
            std::vector<std::string> payloadFiles;
            for (const auto& relativePath : candidateFiles)
            {
                if (shouldDownloadRepoFile(relativePath))
                {
                    payloadFiles.push_back(relativePath);
                }
            }

            if (payloadFiles.empty())
            {
                success = false;
                errorMessage = "No downloadable model payload files were found in the Hugging Face repository";
            }
            else
            {
                bool payloadSuccess = true;
                std::uint64_t expectedPayloadBytes = 0;
                for (const auto& relativePath : payloadFiles)
                {
                    if (const auto sizeIt = siblingSizes.find(relativePath); sizeIt != siblingSizes.end())
                    {
                        expectedPayloadBytes += sizeIt->second;
                    }
                }

                std::uint64_t completedPayloadBytes = 0;
                for (std::size_t index = 0; index < payloadFiles.size(); ++index)
                {
                    const auto& relativePath = payloadFiles[index];
                    const auto fileUrl = "https://huggingface.co/" + percentEncode(remoteId, false) + "/resolve/main/" + percentEncode(relativePath, false);
                    const auto destination = installPath / std::filesystem::path(relativePath);
                    const auto displayName = std::filesystem::path(relativePath).filename().string();
                    const auto expectedFileBytes = [&, relativePath]() -> std::uint64_t
                    {
                        if (const auto it = siblingSizes.find(relativePath); it != siblingSizes.end())
                        {
                            return it->second;
                        }
                        return 0;
                    }();

                    const double base = static_cast<double>(index) / static_cast<double>(payloadFiles.size());
                    std::string initialMessage = "Downloading " + displayName;
                    if (expectedFileBytes > 0)
                    {
                        initialMessage += " (" + humanReadableSize(expectedFileBytes) + ")";
                    }
                    operationState->setProgress(
                        ModelStatus::Downloading,
                        0.05 + base * 0.90,
                        std::move(initialMessage));

                    const auto onProgress = [operationState,
                                             displayName,
                                             index,
                                             totalFiles = payloadFiles.size(),
                                             completedPayloadBytes,
                                             expectedPayloadBytes,
                                             expectedFileBytes](std::uint64_t downloaded, std::uint64_t total)
                    {
                        double overallProgress = 0.05;
                        double fileFraction = 0.0;
                        const auto resolvedTotal = total > 0 ? total : expectedFileBytes;
                        if (resolvedTotal > 0)
                        {
                            fileFraction = std::clamp(static_cast<double>(downloaded) / static_cast<double>(resolvedTotal), 0.0, 1.0);
                        }

                        if (expectedPayloadBytes > 0)
                        {
                            const auto totalCompleted = completedPayloadBytes + std::min<std::uint64_t>(downloaded, resolvedTotal > 0 ? resolvedTotal : downloaded);
                            overallProgress = 0.05 + (static_cast<double>(totalCompleted) / static_cast<double>(expectedPayloadBytes)) * 0.90;
                        }
                        else
                        {
                            overallProgress = 0.05 + ((static_cast<double>(index) + fileFraction) / static_cast<double>(totalFiles)) * 0.90;
                        }

                        std::string message = "Downloading " + displayName;
                        if (resolvedTotal > 0)
                        {
                            message += " " + std::to_string(static_cast<int>(std::round(fileFraction * 100.0))) + "%";
                            message += " (" + humanReadableSize(std::min<std::uint64_t>(downloaded, resolvedTotal)) + " / " + humanReadableSize(resolvedTotal) + ")";
                        }
                        operationState->setProgress(ModelStatus::Downloading, overallProgress, std::move(message));
                    };

                    if (!downloadToFile(fileUrl, destination, operationState, onProgress))
                    {
                        if (operationState->isCancelled())
                        {
                            return handleCancelled();
                        }
                        payloadSuccess = false;
                        errorMessage = "Failed to download Hugging Face model file: " + relativePath;
                        break;
                    }

                    downloadedFiles.push_back(relativePath);
                    completedPayloadBytes += expectedFileBytes;
                }
                success = payloadSuccess;
            }
        }
        else
        {
            errorMessage = "Could not query Hugging Face model metadata";
        }
#else
        errorMessage = "Hugging Face model downloads are currently implemented on Windows only";
        success = false;
#endif
    }
#if defined(_WIN32)
    else
    {
        operationState->setProgress(ModelStatus::Downloading, 0.10, "Downloading model");
        const auto onProgress = [operationState](std::uint64_t downloaded, std::uint64_t total)
        {
            double fraction = 0.0;
            if (total > 0)
            {
                fraction = std::clamp(static_cast<double>(downloaded) / static_cast<double>(total), 0.0, 1.0);
            }

            std::string message = "Downloading model";
            if (total > 0)
            {
                message += " " + std::to_string(static_cast<int>(std::round(fraction * 100.0))) + "%";
            }

            operationState->setProgress(ModelStatus::Downloading, 0.10 + fraction * 0.85, std::move(message));
        };
        success = downloadToFile(descriptor.downloadUri, installPath / "downloaded_model.bin", operationState, onProgress);
        if (success)
        {
            downloadedFiles.push_back("downloaded_model.bin");
        }
    }
#endif

    if (operationState->isCancelled())
    {
        return handleCancelled();
    }

    if (!success)
    {
        std::filesystem::remove_all(installPath, ec);
        operationState->setProgress(ModelStatus::Broken, 1.0, errorMessage.empty() ? "Model download failed" : errorMessage);
        result.errorMessage = errorMessage.empty() ? "Model download failed" : errorMessage;
        return result;
    }

    operationState->setProgress(ModelStatus::Verifying, 0.96, "Verifying model files");
    std::string validationError;
    if (!validateModelFolder(installPath, validationError))
    {
        std::filesystem::remove_all(installPath, ec);
        operationState->setProgress(ModelStatus::Broken, 1.0, validationError);
        result.errorMessage = validationError;
        return result;
    }

    ModelInstallManifest manifest;
    manifest.modelId = descriptor.id;
    manifest.remoteId = descriptor.remoteId;
    manifest.version = descriptor.version;
    manifest.installPath = installPath;
    manifest.files = downloadedFiles;
    saveInstallManifest(manifest, nullptr);

    auto installed = makeInstalledRecord(descriptor, installPath, descriptor.source.empty() ? "download" : descriptor.source, ModelStatus::Ready);
    installed.installedAt = makeTimestamp();
    result.success = true;
    result.installedRecord = installed;
    operationState->setProgress(ModelStatus::Ready, 1.0, "Ready");
    return result;
}

InstalledModelInfo ModelManager::makeInstalledRecord(const ModelDescriptor& descriptor,
                                                     const std::filesystem::path& installPath,
                                                     const std::string& source,
                                                     ModelStatus status) const
{
    InstalledModelInfo info;
    info.id = descriptor.id;
    info.displayName = descriptor.displayName;
    info.version = descriptor.version;
    info.installPath = installPath;
    info.source = source;
    info.status = status;
    info.capabilities = descriptor.capabilities;
    return info;
}

void ModelManager::reconcileInstalledStatuses()
{
    for (const auto& localFolder : scanLocalFolders())
    {
        if (!localFolder.valid)
        {
            continue;
        }

        ModelInstallManifest manifest;
        const bool hasManifest = loadInstallManifest(localFolder.path, manifest) && !manifest.modelId.empty();
        auto descriptor = hasManifest
            ? findCatalogModel(manifest.modelId)
            : inferCatalogDescriptorForLocalFolder(catalog_, localFolder.path);
        if (!descriptor.has_value())
        {
            logger_.warning("Valid local model folder could not be matched to a catalog entry and was left in Local: " + localFolder.path.string());
            continue;
        }

        const auto modelId = hasManifest ? manifest.modelId : descriptor->id;
        if (installed_.find(modelId) != installed_.end())
        {
            continue;
        }

        if (!hasManifest)
        {
            manifest.modelId = descriptor->id;
            manifest.remoteId = descriptor->remoteId;
            manifest.version = descriptor->version;
            manifest.installPath = localFolder.path;
            manifest.files = collectModelFilesForManifest(localFolder.path);
            saveInstallManifest(manifest, nullptr);
            logger_.info("Created install manifest for local model folder: " + localFolder.path.string());
        }

        auto recovered = makeInstalledRecord(*descriptor, localFolder.path, "existing-folder", ModelStatus::Ready);
        recovered.installedAt = makeTimestamp();
        installed_[modelId] = recovered;
        for (const auto capability : descriptor->capabilities)
        {
            if (activeBindings_[capability].empty())
            {
                activeBindings_[capability] = modelId;
            }
        }
        logger_.info("Recovered installed model from local folder: " + modelId);
    }

    for (auto& [id, installed] : installed_)
    {
        std::string validationError;
        installed.status = validateModelFolder(installed.installPath, validationError) ? ModelStatus::Ready : ModelStatus::Broken;
        if (const auto catalog = findCatalogModel(id); catalog.has_value() && !catalog->version.empty() && catalog->version != installed.version)
        {
            installed.status = ModelStatus::UpdateAvailable;
        }
    }

    for (const auto capability : {ModelCapability::SongGeneration, ModelCapability::TrackGeneration, ModelCapability::TrackSeparation, ModelCapability::VocalToBgm})
    {
        const auto binding = activeBindings_.find(capability);
        if (binding != activeBindings_.end())
        {
            const auto resolved = resolveInstalledModel(binding->second);
            if (resolved.has_value())
            {
                continue;
            }

            activeBindings_.erase(binding);
        }

        for (const auto& [modelId, installed] : installed_)
        {
            if ((installed.status != ModelStatus::Ready && installed.status != ModelStatus::UpdateAvailable)
                || std::find(installed.capabilities.begin(), installed.capabilities.end(), capability) == installed.capabilities.end())
            {
                continue;
            }

            activeBindings_[capability] = modelId;
            break;
        }
    }
}
}
