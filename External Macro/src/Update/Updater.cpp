#include "Update/Updater.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Logging/Logger.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

namespace Update
{
namespace
{

constexpr long long kMaxClockSkewSeconds = 5 * 60; // 5-minute replay window (same as LicenseClient)
constexpr int kHttpDownloadTimeoutMs = 30000;
constexpr int kCheckIntervalSeconds = 4 * 60 * 60; // re-check every 4 hours

bool IsAllZeros(
    const std::array<std::uint8_t, 32>& key)
{
    for (const auto byte : key)
    {
        if (byte != 0)
            return false;
    }

    return true;
}

std::string ToLowerAscii(
    const std::string& input)
{
    std::string out;
    out.reserve(input.size());

    for (const char c : input)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return out;
}

std::string ToUpperTrimmed(
    const std::string& value)
{
    std::size_t first = 0;

    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;

    std::size_t last = value.size();

    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;

    std::string out;
    out.reserve(last - first);

    for (std::size_t i = first; i < last; ++i)
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));

    return out;
}

bool IsValidHex64(
    const std::string& s)
{
    if (s.size() != 64)
        return false;

    for (const char c : s)
    {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }

    return true;
}

const char* SkipWhitespace(
    const char* p,
    const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;

    return p;
}

// Skips a single JSON value (string, number, boolean, null, object, or array)
// starting at p and returns a pointer just past the value, or nullptr on error.
const char* SkipJsonValue(
    const char* p,
    const char* end)
{
    p = SkipWhitespace(p, end);

    if (p >= end)
        return nullptr;

    if (*p == '"')
    {
        ++p;
        bool escaped = false;

        while (p < end)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (*p == '\\')
            {
                escaped = true;
            }
            else if (*p == '"')
            {
                return p + 1;
            }

            ++p;
        }

        return nullptr;
    }

    if (*p == '{' || *p == '[')
    {
        const char openChar = *p;
        const char closeChar = (openChar == '{') ? '}' : ']';
        int depth = 0;
        bool inString = false;
        bool escaped = false;

        while (p < end)
        {
            const char c = *p;

            if (inString)
            {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (c == '"')
                    inString = false;
            }
            else
            {
                if (c == '"')
                {
                    inString = true;
                }
                else if (c == openChar)
                {
                    ++depth;
                }
                else if (c == closeChar)
                {
                    --depth;

                    if (depth == 0)
                        return p + 1;
                }
            }

            ++p;
        }

        return nullptr;
    }

    // Bare token: number, true, false, null.
    const char* start = p;

    while (p < end &&
        *p != ',' && *p != '}' && *p != ']' &&
        *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
    {
        ++p;
    }

    return (p > start) ? p : nullptr;
}

// Finds a direct child field "fieldName" of the JSON object spanning [objBegin, objEnd).
bool FindDirectObjectField(
    const char* objBegin,
    const char* objEnd,
    const std::string& fieldName,
    const char*& outValueBegin,
    const char*& outValueEnd)
{
    const char* p = SkipWhitespace(objBegin, objEnd);

    if (p >= objEnd || *p != '{')
        return false;

    ++p;

    while (p < objEnd)
    {
        p = SkipWhitespace(p, objEnd);

        if (p >= objEnd)
            return false;

        if (*p == '}')
            return false;

        if (*p == ',')
        {
            ++p;
            continue;
        }

        if (*p != '"')
            return false;

        // Parse key string.
        ++p;
        std::string key;
        bool escaped = false;

        while (p < objEnd)
        {
            if (escaped)
            {
                key += *p;
                escaped = false;
            }
            else if (*p == '\\')
            {
                escaped = true;
            }
            else if (*p == '"')
            {
                break;
            }
            else
            {
                key += *p;
            }

            ++p;
        }

        if (p >= objEnd || *p != '"')
            return false;

        ++p;
        p = SkipWhitespace(p, objEnd);

        if (p >= objEnd || *p != ':')
            return false;

        p = SkipWhitespace(p + 1, objEnd);
        const char* valEnd = SkipJsonValue(p, objEnd);

        if (valEnd == nullptr)
            return false;

        if (key == fieldName)
        {
            outValueBegin = p;
            outValueEnd = valEnd;
            return true;
        }

        p = valEnd;
    }

    return false;
}

bool ExtractStringField(
    const char* objBegin,
    const char* objEnd,
    const std::string& fieldName,
    std::string& out)
{
    const char* valBegin = nullptr;
    const char* valEnd = nullptr;

    if (!FindDirectObjectField(objBegin, objEnd, fieldName, valBegin, valEnd))
        return false;

    if (valEnd - valBegin < 2 || *valBegin != '"' || valEnd[-1] != '"')
        return false;

    std::string unescaped;
    bool escaped = false;

    for (const char* q = valBegin + 1; q < valEnd - 1; ++q)
    {
        if (escaped)
        {
            switch (*q)
            {
            case '"': unescaped += '"'; break;
            case '\\': unescaped += '\\'; break;
            case '/': unescaped += '/'; break;
            case 'n': unescaped += '\n'; break;
            case 'r': unescaped += '\r'; break;
            case 't': unescaped += '\t'; break;
            default: unescaped += *q; break;
            }

            escaped = false;
        }
        else if (*q == '\\')
        {
            escaped = true;
        }
        else
        {
            unescaped += *q;
        }
    }

    out = std::move(unescaped);
    return true;
}

bool ExtractInt64Field(
    const char* objBegin,
    const char* objEnd,
    const std::string& fieldName,
    long long& out)
{
    const char* valBegin = nullptr;
    const char* valEnd = nullptr;

    if (!FindDirectObjectField(objBegin, objEnd, fieldName, valBegin, valEnd))
        return false;

    const std::string token(valBegin, valEnd);

    if (token == "null")
    {
        out = 0;
        return true;
    }

    try
    {
        std::size_t idx = 0;
        const long long v = std::stoll(token, &idx, 10);

        if (idx != token.size())
            return false;

        out = v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ExtractUint64Field(
    const char* objBegin,
    const char* objEnd,
    const std::string& fieldName,
    std::uint64_t& out)
{
    const char* valBegin = nullptr;
    const char* valEnd = nullptr;

    if (!FindDirectObjectField(objBegin, objEnd, fieldName, valBegin, valEnd))
        return false;

    const std::string token(valBegin, valEnd);

    if (token.empty() || token.front() == '-')
        return false;

    try
    {
        std::size_t idx = 0;
        const unsigned long long v = std::stoull(token, &idx, 10);

        if (idx != token.size())
            return false;

        out = static_cast<std::uint64_t>(v);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::wstring Utf8ToWide(
    const std::string& str)
{
    if (str.empty())
        return std::wstring();

    const int needed = MultiByteToWideChar(
        CP_UTF8, 0,
        str.data(), static_cast<int>(str.size()),
        nullptr, 0);

    if (needed <= 0)
        return std::wstring(str.begin(), str.end());

    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0,
        str.data(), static_cast<int>(str.size()),
        wide.data(), needed);

    return wide;
}

std::string WideToUtf8(
    const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    const int needed = WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        nullptr, 0,
        nullptr, nullptr);

    if (needed <= 0)
        return std::string();

    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        out.data(), needed,
        nullptr, nullptr);

    return out;
}

bool PerformDownloadAttempt(
    const std::wstring& wideHost,
    INTERNET_PORT port,
    const std::wstring& widePath,
    const std::string& destPath,
    std::uint64_t maxBytes,
    DWORD accessType,
    ProgressCallback onProgress,
    std::string& outError)
{
    HINTERNET session = WinHttpOpen(
        L"ExternalMacro/1.0",
        accessType,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (session == nullptr)
    {
        outError = Licensing::WinHttpErrorText("WinHttpOpen", GetLastError());
        return false;
    }

    WinHttpSetTimeouts(
        session,
        kHttpDownloadTimeoutMs,
        kHttpDownloadTimeoutMs,
        kHttpDownloadTimeoutMs,
        kHttpDownloadTimeoutMs);

    // Follow redirects automatically (GitHub release asset URLs redirect to a CDN),
    // but disallow HTTPS -> HTTP downgrade redirects.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(
        session,
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirectPolicy,
        sizeof(redirectPolicy));

    HINTERNET connection = WinHttpConnect(
        session,
        wideHost.c_str(),
        port,
        0);

    if (connection == nullptr)
    {
        outError = Licensing::WinHttpErrorText("WinHttpConnect", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"GET",
        widePath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);

    if (request == nullptr)
    {
        outError = Licensing::WinHttpErrorText("WinHttpOpenRequest", GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpSetOption(
        request,
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirectPolicy,
        sizeof(redirectPolicy));

    const BOOL sent = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);

    if (sent == FALSE)
    {
        outError = Licensing::WinHttpErrorText("WinHttpSendRequest", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    if (WinHttpReceiveResponse(request, nullptr) == FALSE)
    {
        outError = Licensing::WinHttpErrorText("WinHttpReceiveResponse", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    if (statusCode < 200 || statusCode >= 300)
    {
        outError = "HTTP " + std::to_string(statusCode);
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    // Check Content-Length header when present so oversized responses are
    // rejected immediately before downloading.
    std::uint64_t contentLength = 0;
    {
        wchar_t lengthBuf[64]{};
        DWORD lengthBytes = sizeof(lengthBuf);

        if (WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX,
            lengthBuf,
            &lengthBytes,
            WINHTTP_NO_HEADER_INDEX) == TRUE)
        {
            contentLength = static_cast<std::uint64_t>(_wcstoui64(lengthBuf, nullptr, 10));

            if (contentLength > maxBytes)
            {
                outError = "Update package exceeds maximum size limit";
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return false;
            }
        }
    }

    const std::wstring wideDest = Utf8ToWide(destPath);
    HANDLE file = CreateFileW(
        wideDest.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        outError = "Could not create temporary update file";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    std::uint64_t totalRead = 0;
    DWORD available = 0;
    bool ok = true;
    std::vector<char> buffer(64 * 1024);

    while (WinHttpQueryDataAvailable(request, &available) == TRUE && available > 0)
    {
        while (available > 0)
        {
            const DWORD toRead = static_cast<DWORD>(
                std::min<std::size_t>(available, buffer.size()));
            DWORD downloaded = 0;

            if (WinHttpReadData(
                request,
                buffer.data(),
                toRead,
                &downloaded) == FALSE)
            {
                outError = Licensing::WinHttpErrorText("WinHttpReadData", GetLastError());
                ok = false;
                break;
            }

            if (downloaded == 0)
                break;

            totalRead += downloaded;
            available -= downloaded;

            if (totalRead > maxBytes)
            {
                outError = "Update package exceeded maximum size limit during download";
                ok = false;
                break;
            }

            DWORD written = 0;

            if (WriteFile(file, buffer.data(), downloaded, &written, nullptr) == FALSE ||
                written != downloaded)
            {
                outError = "Failed writing update package to disk";
                ok = false;
                break;
            }

            if (onProgress)
                onProgress(totalRead, contentLength);
        }

        if (!ok)
            break;
    }

    FlushFileBuffers(file);
    CloseHandle(file);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!ok || totalRead == 0)
    {
        DeleteFileW(wideDest.c_str());

        if (outError.empty())
            outError = "Downloaded update file was empty";

        return false;
    }

    return true;
}

} // namespace

bool ParseSemVer(
    const std::string& version,
    SemVer& out)
{
    out = SemVer{};

    std::size_t first = 0;

    while (first < version.size() && std::isspace(static_cast<unsigned char>(version[first])))
        ++first;

    std::size_t last = version.size();

    while (last > first && std::isspace(static_cast<unsigned char>(version[last - 1])))
        --last;

    if (first < last && (version[first] == 'v' || version[first] == 'V'))
        ++first;

    if (first >= last)
        return false;

    const std::string trimmed = version.substr(first, last - first);
    const std::size_t dot1 = trimmed.find('.');

    if (dot1 == std::string::npos)
        return false;

    const std::size_t dot2 = trimmed.find('.', dot1 + 1);

    if (dot2 == std::string::npos)
        return false;

    if (trimmed.find('.', dot2 + 1) != std::string::npos)
        return false;

    const std::string sMajor = trimmed.substr(0, dot1);
    const std::string sMinor = trimmed.substr(dot1 + 1, dot2 - dot1 - 1);
    const std::string sPatch = trimmed.substr(dot2 + 1);

    auto parsePart = [](const std::string& part, int& value) -> bool
    {
        if (part.empty() || part.size() > 9)
            return false;

        for (const char c : part)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
        }

        try
        {
            value = std::stoi(part);
            return value >= 0;
        }
        catch (...)
        {
            return false;
        }
    };

    return parsePart(sMajor, out.major) &&
        parsePart(sMinor, out.minor) &&
        parsePart(sPatch, out.patch);
}

int CompareSemVer(
    const std::string& a,
    const std::string& b)
{
    SemVer va{};
    SemVer vb{};

    if (!ParseSemVer(a, va) || !ParseSemVer(b, vb))
        return 0;

    if (va.major != vb.major)
        return (va.major > vb.major) ? 1 : -1;

    if (va.minor != vb.minor)
        return (va.minor > vb.minor) ? 1 : -1;

    if (va.patch != vb.patch)
        return (va.patch > vb.patch) ? 1 : -1;

    return 0;
}

bool IsNewerVersion(
    const std::string& candidateVersion,
    const std::string& currentVersion)
{
    SemVer vc{};
    SemVer vcur{};

    if (!ParseSemVer(candidateVersion, vc) || !ParseSemVer(currentVersion, vcur))
        return false;

    return CompareSemVer(candidateVersion, currentVersion) > 0;
}

bool ParseUpdateResponse(
    const std::string& body,
    UpdateCheckResponse& out)
{
    out = UpdateCheckResponse{};

    const char* begin = body.data();
    const char* end = begin + body.size();

    if (!ExtractStringField(begin, end, "status", out.status) || out.status.empty())
        return false;

    ExtractStringField(begin, end, "reason", out.reason);
    ExtractStringField(begin, end, "nonce", out.nonce);
    ExtractStringField(begin, end, "sig", out.sigHex);
    ExtractInt64Field(begin, end, "ts", out.ts);

    const char* manifestBegin = nullptr;
    const char* manifestEnd = nullptr;

    if (FindDirectObjectField(begin, end, "manifest", manifestBegin, manifestEnd) &&
        manifestBegin < manifestEnd && *manifestBegin == '{')
    {
        ExtractStringField(manifestBegin, manifestEnd, "version", out.manifest.version);
        ExtractStringField(manifestBegin, manifestEnd, "url", out.manifest.url);
        ExtractStringField(manifestBegin, manifestEnd, "sha256", out.manifest.sha256);
        ExtractUint64Field(manifestBegin, manifestEnd, "size", out.manifest.size);
        ExtractStringField(manifestBegin, manifestEnd, "notes", out.manifest.notes);
        ExtractStringField(manifestBegin, manifestEnd, "releaseSig", out.manifest.releaseSig);
    }

    return true;
}

std::string BuildUpdateSignedMessage(
    const std::string& version,
    const std::string& url,
    const std::string& sha256,
    const std::string& nonce,
    long long ts)
{
    return "update|" + version + "|" + url + "|" + sha256 + "|" + nonce + "|" + std::to_string(ts);
}

std::string BuildCurrentSignedMessage(
    const std::string& nonce,
    long long ts)
{
    return "current|" + nonce + "|" + std::to_string(ts);
}

std::string BuildReleaseSignedMessage(
    const std::string& version,
    const std::string& sha256,
    std::uint64_t size)
{
    return "release|" + version + "|" + sha256 + "|" + std::to_string(size);
}

bool VerifyUpdateCheckResponse(
    const UpdateCheckResponse& response,
    const std::string& expectedNonce,
    const std::string& currentVersion,
    const std::array<std::uint8_t, 32>& serverPublicKey,
    long long nowUnixSeconds,
    bool& outHasUpdate,
    std::string& outReason)
{
    outHasUpdate = false;
    outReason.clear();

    if (IsAllZeros(serverPublicKey))
    {
        outReason = "not_configured";
        return false;
    }

    if (response.status != "ok" && response.status != "current")
    {
        outReason = response.reason.empty() ? "invalid" : response.reason;
        return false;
    }

    // 1. Verify nonce and timestamp window (same 5-minute check as LicenseClient).
    if (response.nonce != expectedNonce || response.nonce.empty())
    {
        outReason = "nonce_mismatch";
        return false;
    }

    const long long skew = nowUnixSeconds - response.ts;

    if (skew > kMaxClockSkewSeconds || skew < -kMaxClockSkewSeconds)
    {
        outReason = "stale_response";
        return false;
    }

    // 2. Verify server Ed25519 signature over the response payload.
    std::array<std::uint8_t, 64> signature{};

    if (!Licensing::HexToBytes(response.sigHex, signature.data(), signature.size()))
    {
        outReason = "bad_signature";
        return false;
    }

    if (response.status == "current")
    {
        const std::string message = BuildCurrentSignedMessage(response.nonce, response.ts);

        if (!Licensing::VerifyEd25519(serverPublicKey, message, signature))
        {
            outReason = "bad_signature";
            return false;
        }

        outHasUpdate = false;
        return true;
    }

    const std::string message = BuildUpdateSignedMessage(
        response.manifest.version,
        response.manifest.url,
        response.manifest.sha256,
        response.nonce,
        response.ts);

    if (!Licensing::VerifyEd25519(serverPublicKey, message, signature))
    {
        outReason = "bad_signature";
        return false;
    }

    // 3. Verify manifest version is strictly greater than currentVersion (rejects downgrades and same version).
    SemVer candidateSemVer{};
    SemVer currentSemVer{};

    if (!ParseSemVer(response.manifest.version, candidateSemVer) ||
        !ParseSemVer(currentVersion, currentSemVer))
    {
        outReason = "invalid_version";
        return false;
    }

    if (CompareSemVer(response.manifest.version, currentVersion) <= 0)
    {
        outReason = "downgrade_rejected";
        return false;
    }

    // 4. Verify download URL starts with https://.
    if (response.manifest.url.rfind("https://", 0) != 0 ||
        response.manifest.url.size() <= 8)
    {
        outReason = "insecure_url";
        return false;
    }

    if (response.manifest.size == 0 || response.manifest.size > kMaxUpdateSizeBytes)
    {
        outReason = "invalid_size";
        return false;
    }

    if (!IsValidHex64(response.manifest.sha256))
    {
        outReason = "invalid_sha256";
        return false;
    }

    outHasUpdate = true;
    return true;
}

bool ComputeSha256Hex(
    const std::uint8_t* data,
    std::size_t size,
    std::string& outHex)
{
    outHex.clear();

    BCRYPT_ALG_HANDLE alg = nullptr;

    if (BCryptOpenAlgorithmProvider(
        &alg,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0) < 0)
    {
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<std::uint8_t, 32> digest{};
    bool ok = false;

    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0)
    {
        if (BCryptHashData(
            hash,
            const_cast<PUCHAR>(data),
            static_cast<ULONG>(size),
            0) >= 0 &&
            BCryptFinishHash(
                hash,
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0) >= 0)
        {
            outHex = Licensing::BytesToHex(digest.data(), digest.size());
            ok = true;
        }

        BCryptDestroyHash(hash);
    }

    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool ComputeFileSha256Hex(
    const std::string& filePath,
    std::uint64_t& outSize,
    std::string& outHex)
{
    outSize = 0;
    outHex.clear();

    const std::wstring widePath = Utf8ToWide(filePath);
    HANDLE file = CreateFileW(
        widePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER fileSize{};

    if (GetFileSizeEx(file, &fileSize) == FALSE || fileSize.QuadPart < 0)
    {
        CloseHandle(file);
        return false;
    }

    BCRYPT_ALG_HANDLE alg = nullptr;

    if (BCryptOpenAlgorithmProvider(
        &alg,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0) < 0)
    {
        CloseHandle(file);
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;

    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(alg, 0);
        CloseHandle(file);
        return false;
    }

    std::vector<std::uint8_t> buffer(64 * 1024);
    DWORD bytesRead = 0;
    std::uint64_t total = 0;
    bool ok = true;

    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) == TRUE &&
        bytesRead > 0)
    {
        total += bytesRead;

        if (BCryptHashData(hash, buffer.data(), bytesRead, 0) < 0)
        {
            ok = false;
            break;
        }
    }

    std::array<std::uint8_t, 32> digest{};

    if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0)
    {
        outSize = total;
        outHex = Licensing::BytesToHex(digest.data(), digest.size());
    }
    else
    {
        ok = false;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    return ok;
}

bool VerifyDownloadedPackage(
    const std::string& filePath,
    const UpdateManifest& manifest,
    const std::array<std::uint8_t, 32>& releasePublicKey,
    std::string& outReason)
{
    outReason.clear();

    if (manifest.size == 0 || manifest.size > kMaxUpdateSizeBytes)
    {
        outReason = "invalid_size";
        return false;
    }

    std::uint64_t actualSize = 0;
    std::string actualSha256;

    if (!ComputeFileSha256Hex(filePath, actualSize, actualSha256))
    {
        outReason = "file_read_error";
        return false;
    }

    // 1. Size matches manifest.size.
    if (actualSize != manifest.size)
    {
        outReason = "size_mismatch";
        return false;
    }

    // 2. SHA-256 matches manifest.sha256.
    const std::string expectedSha256 = ToLowerAscii(manifest.sha256);

    if (!IsValidHex64(expectedSha256) || actualSha256 != expectedSha256)
    {
        outReason = "hash_mismatch";
        return false;
    }

    // 3. releaseSig verifies against the embedded release public key.
    if (IsAllZeros(releasePublicKey))
    {
        outReason = "release_key_not_configured";
        return false;
    }

    std::array<std::uint8_t, 64> releaseSigBytes{};

    if (!Licensing::HexToBytes(manifest.releaseSig, releaseSigBytes.data(), releaseSigBytes.size()))
    {
        outReason = "bad_release_signature";
        return false;
    }

    const std::string releaseMessage = BuildReleaseSignedMessage(
        manifest.version,
        manifest.sha256,
        manifest.size);

    if (!Licensing::VerifyEd25519(releasePublicKey, releaseMessage, releaseSigBytes))
    {
        outReason = "bad_release_signature";
        return false;
    }

    return true;
}

Licensing::HttpResult WinHttpUpdateTransport::PostUpdate(
    const std::string& serverUrl,
    const std::string& body)
{
    return Licensing::WinHttpPostJson(serverUrl, "/update", body);
}

bool WinHttpUpdateTransport::DownloadFile(
    const std::string& url,
    const std::string& destPath,
    std::uint64_t maxBytes,
    ProgressCallback onProgress,
    std::string& outError)
{
    outError.clear();

    if (url.rfind("https://", 0) != 0)
    {
        outError = "Download URL must use https://";
        return false;
    }

    const std::wstring wideUrl = Utf8ToWide(url);
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);

    wchar_t hostBuf[256]{};
    wchar_t pathBuf[2048]{};
    urlComp.lpszHostName = hostBuf;
    urlComp.dwHostNameLength = static_cast<DWORD>(std::size(hostBuf));
    urlComp.lpszUrlPath = pathBuf;
    urlComp.dwUrlPathLength = static_cast<DWORD>(std::size(pathBuf));

    if (WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &urlComp) == FALSE ||
        urlComp.nScheme != INTERNET_SCHEME_HTTPS)
    {
        outError = "Invalid HTTPS download URL";
        return false;
    }

    const std::wstring wideHost(urlComp.lpszHostName, urlComp.dwHostNameLength);
    const std::wstring widePath = (urlComp.dwUrlPathLength > 0)
        ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength)
        : std::wstring(L"/");

    std::string firstError;

    if (PerformDownloadAttempt(
        wideHost,
        urlComp.nPort,
        widePath,
        destPath,
        maxBytes,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        onProgress,
        firstError))
    {
        return true;
    }

    std::string directError;

    if (PerformDownloadAttempt(
        wideHost,
        urlComp.nPort,
        widePath,
        destPath,
        maxBytes,
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        onProgress,
        directError))
    {
        return true;
    }

    outError = directError.empty() ? firstError : directError;
    return false;
}

Updater::Updater(
    std::string serverUrl,
    std::array<std::uint8_t, 32> serverPublicKey,
    std::array<std::uint8_t, 32> releasePublicKey,
    std::string currentVersion,
    std::unique_ptr<IUpdateTransport> transport)
    : m_ServerUrl(std::move(serverUrl))
    , m_ServerPublicKey(serverPublicKey)
    , m_ReleasePublicKey(releasePublicKey)
    , m_CurrentVersion(std::move(currentVersion))
    , m_Transport(std::move(transport))
{
}

Updater::~Updater()
{
    StopBackgroundLoop();

    if (m_DownloadThread.joinable())
        m_DownloadThread.join();
}

void Updater::CleanupOldFiles()
{
    CleanupOldFilesInDir(ExeDirectory());
}

void Updater::CleanupOldFilesInDir(
    const std::string& exeDir)
{
    if (exeDir.empty())
        return;

    const std::wstring wideDir = Utf8ToWide(exeDir);
    const std::wstring oldExe = wideDir + L"\\dribtard.old.exe";
    const std::wstring newExe = wideDir + L"\\dribtard.new.exe";

    DeleteFileW(oldExe.c_str());
    DeleteFileW(newExe.c_str());
}

UpdateState Updater::CheckNow(
    const std::string& licenseKey,
    const std::string& hwid)
{
    const std::string normalizedKey = ToUpperTrimmed(licenseKey);

    if (normalizedKey.empty() || hwid.empty())
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_State;
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        // Do not clobber an active download or a ready-to-apply update.
        if (m_State.status == UpdateStatus::Downloading ||
            m_State.status == UpdateStatus::ReadyToApply)
        {
            return m_State;
        }
    }

    const std::string nonce = Licensing::GenerateNonceHex();
    const std::string requestBody =
        "{\"key\":\"" + Licensing::JsonEscape(normalizedKey) +
        "\",\"hwid\":\"" + Licensing::JsonEscape(hwid) +
        "\",\"nonce\":\"" + Licensing::JsonEscape(nonce) +
        "\",\"currentVersion\":\"" + Licensing::JsonEscape(m_CurrentVersion) + "\"}";

    Licensing::HttpResult http;

    try
    {
        http = m_Transport->PostUpdate(m_ServerUrl, requestBody);
    }
    catch (...)
    {
        http.ok = false;
        http.error = "transport exception";
    }

    if (!http.ok)
    {
        Logger::Error("Update check failed: " + http.error);

        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_State.status != UpdateStatus::UpdateAvailable)
        {
            m_State.status = UpdateStatus::Failed;
            m_State.errorReason = "network_error";
            m_State.statusText = "Update check failed.";
        }

        return m_State;
    }

    UpdateCheckResponse parsed;

    if (!ParseUpdateResponse(http.body, parsed))
    {
        Logger::Error("Update check failed: unreadable server response.");

        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_State.status != UpdateStatus::UpdateAvailable)
        {
            m_State.status = UpdateStatus::Failed;
            m_State.errorReason = "bad_response";
            m_State.statusText = "Update server returned an unreadable response.";
        }

        return m_State;
    }

    bool hasUpdate = false;
    std::string reason;

    if (!VerifyUpdateCheckResponse(
        parsed,
        nonce,
        m_CurrentVersion,
        m_ServerPublicKey,
        Licensing::NowUnixSeconds(),
        hasUpdate,
        reason))
    {
        Logger::Error("Update response verification failed: " + reason);

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State.status = UpdateStatus::Failed;
        m_State.errorReason = reason;
        m_State.statusText = "Update check rejected (" + reason + ").";
        return m_State;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!hasUpdate)
    {
        m_State.status = UpdateStatus::UpToDate;
        m_State.manifest = UpdateManifest{};
        m_State.errorReason.clear();
        m_State.statusText = "Up to date.";
        Logger::Info("Application is up to date (v" + m_CurrentVersion + ").");
        return m_State;
    }

    m_State.status = UpdateStatus::UpdateAvailable;
    m_State.manifest = parsed.manifest;
    m_State.progress = 0.0f;
    m_State.downloadedBytes = 0;
    m_State.totalBytes = parsed.manifest.size;
    m_State.errorReason.clear();
    m_State.statusText = "Version " + parsed.manifest.version + " available";

    Logger::Info("Update available: v" + parsed.manifest.version);
    return m_State;
}

UpdateState Updater::DownloadAndVerifyNow(
    const std::string& destDirectory)
{
    UpdateManifest manifest;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_State.manifest.version.empty() || m_State.manifest.url.empty())
        {
            m_State.status = UpdateStatus::Failed;
            m_State.errorReason = "no_manifest";
            m_State.statusText = "No update manifest available.";
            return m_State;
        }

        manifest = m_State.manifest;
        m_State.status = UpdateStatus::Downloading;
        m_State.progress = 0.0f;
        m_State.downloadedBytes = 0;
        m_State.totalBytes = manifest.size;
        m_State.errorReason.clear();
        m_State.statusText = "Downloading v" + manifest.version + "...";
    }

    // Re-check version, URL scheme, and size cap before starting the download.
    if (!IsNewerVersion(manifest.version, m_CurrentVersion))
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State.status = UpdateStatus::Failed;
        m_State.errorReason = "downgrade_rejected";
        m_State.statusText = "Update rejected: version is not newer.";
        return m_State;
    }

    if (manifest.url.rfind("https://", 0) != 0)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State.status = UpdateStatus::Failed;
        m_State.errorReason = "insecure_url";
        m_State.statusText = "Update rejected: insecure download URL.";
        return m_State;
    }

    if (manifest.size == 0 || manifest.size > kMaxUpdateSizeBytes)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State.status = UpdateStatus::Failed;
        m_State.errorReason = "invalid_size";
        m_State.statusText = "Update rejected: invalid size.";
        return m_State;
    }

    const std::string dir = destDirectory.empty() ? ExeDirectory() : destDirectory;
    const std::string newExePath = dir + "\\dribtard.new.exe";

    DeleteFileW(Utf8ToWide(newExePath).c_str());

    std::string downloadError;
    const bool downloaded = m_Transport->DownloadFile(
        manifest.url,
        newExePath,
        kMaxUpdateSizeBytes,
        [this, expectedTotal = manifest.size](std::uint64_t bytesRead, std::uint64_t totalBytes)
        {
            const std::uint64_t denom = (expectedTotal > 0) ? expectedTotal : totalBytes;
            const float fraction = (denom > 0)
                ? std::clamp(static_cast<float>(bytesRead) / static_cast<float>(denom), 0.0f, 1.0f)
                : 0.0f;

            std::lock_guard<std::mutex> lock(m_Mutex);
            m_State.downloadedBytes = bytesRead;
            m_State.totalBytes = denom;
            m_State.progress = fraction;
        },
        downloadError);

    if (!downloaded)
    {
        DeleteFileW(Utf8ToWide(newExePath).c_str());
        Logger::Error("Update download failed: " + downloadError);

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State.status = UpdateStatus::Failed;
        m_State.errorReason = "download_failed";
        m_State.statusText = "Update download failed.";
        return m_State;
    }

    std::string verifyReason;

    if (!VerifyDownloadedPackage(newExePath, manifest, m_ReleasePublicKey, verifyReason))
    {
        DeleteFileW(Utf8ToWide(newExePath).c_str());
        Logger::Error("Downloaded update failed verification (" + verifyReason + "). Aborting update.");

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State.status = UpdateStatus::Failed;
        m_State.errorReason = verifyReason;
        m_State.statusText = "Update verification failed (" + verifyReason + ").";
        return m_State;
    }

    Logger::Info("Downloaded and verified update v" + manifest.version + " at " + newExePath);

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_State.status = UpdateStatus::ReadyToApply;
    m_State.progress = 1.0f;
    m_State.downloadedBytes = manifest.size;
    m_State.totalBytes = manifest.size;
    m_State.stagedPath = newExePath;
    m_State.errorReason.clear();
    m_State.statusText = "Update v" + manifest.version + " verified and ready.";
    return m_State;
}

void Updater::StartBackgroundLoop()
{
    StopBackgroundLoop();

    m_BackgroundStop.store(false);
    m_BackgroundThread = std::thread(&Updater::BackgroundThreadMain, this);
}

void Updater::StopBackgroundLoop()
{
    m_BackgroundStop.store(true);

    if (m_BackgroundThread.joinable())
        m_BackgroundThread.join();
}

void Updater::NotifyLicenseValid(
    const std::string& licenseKey)
{
    const std::string normalized = ToUpperTrimmed(licenseKey);

    if (normalized.empty())
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    const bool firstKey = m_LicenseKey.empty();
    m_LicenseKey = normalized;

    if (firstKey || m_State.status == UpdateStatus::Idle)
        m_CheckRequested = true;
}

void Updater::StartDownloadAsync()
{
    if (m_Downloading.exchange(true))
        return;

    if (m_DownloadThread.joinable())
        m_DownloadThread.join();

    m_DownloadThread = std::thread(&Updater::DownloadThreadMain, this);
}

UpdateState Updater::CurrentState() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_State;
}

bool Updater::ApplyAndRestart(
    std::string& outError)
{
    outError.clear();

    UpdateManifest manifest;
    std::string stagedPath;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_State.status != UpdateStatus::ReadyToApply || m_State.stagedPath.empty())
        {
            outError = "Update is not ready to apply";
            return false;
        }

        manifest = m_State.manifest;
        stagedPath = m_State.stagedPath;
    }

    // Re-verify the staged file right before swapping executables.
    std::string verifyReason;

    if (!VerifyDownloadedPackage(stagedPath, manifest, m_ReleasePublicKey, verifyReason))
    {
        DeleteFileW(Utf8ToWide(stagedPath).c_str());
        outError = "Staged update failed verification: " + verifyReason;
        Logger::Error(outError);
        return false;
    }

    const std::wstring currentExe = RunningExePathW();

    if (currentExe.empty())
    {
        outError = "Could not resolve running executable path";
        return false;
    }

    const std::size_t slash = currentExe.find_last_of(L"\\/");
    const std::wstring exeDir = (slash != std::wstring::npos)
        ? currentExe.substr(0, slash)
        : L".";
    const std::wstring newExe = Utf8ToWide(stagedPath);
    const std::wstring oldExe = exeDir + L"\\dribtard.old.exe";

    return ApplyStagedUpdateForPaths(
        currentExe,
        newExe,
        oldExe,
        true,
        outError);
}

bool Updater::ApplyStagedUpdateForPaths(
    const std::wstring& currentExePath,
    const std::wstring& newExePath,
    const std::wstring& oldExePath,
    bool launchNewProcess,
    std::string& outError)
{
    outError.clear();

    if (GetFileAttributesW(newExePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        outError = "Staged new executable does not exist";
        return false;
    }

    // Remove any leftover .old.exe before renaming the running executable.
    DeleteFileW(oldExePath.c_str());

    if (MoveFileExW(
        currentExePath.c_str(),
        oldExePath.c_str(),
        MOVEFILE_REPLACE_EXISTING) == FALSE)
    {
        outError = "Failed to rename running executable to dribtard.old.exe (" +
            std::to_string(GetLastError()) + ")";
        Logger::Error(outError);
        return false;
    }

    if (MoveFileExW(
        newExePath.c_str(),
        currentExePath.c_str(),
        MOVEFILE_REPLACE_EXISTING) == FALSE)
    {
        const DWORD moveErr = GetLastError();

        // Roll back the running executable name if putting the new exe in place fails.
        MoveFileExW(
            oldExePath.c_str(),
            currentExePath.c_str(),
            MOVEFILE_REPLACE_EXISTING);

        outError = "Failed to move dribtard.new.exe into place (" +
            std::to_string(moveErr) + ")";
        Logger::Error(outError);
        return false;
    }

    if (launchNewProcess)
    {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        const std::size_t slash = currentExePath.find_last_of(L"\\/");
        const std::wstring workDir = (slash != std::wstring::npos)
            ? currentExePath.substr(0, slash)
            : std::wstring();

        std::wstring cmdLine = L"\"" + currentExePath + L"\"";

        if (CreateProcessW(
            currentExePath.c_str(),
            cmdLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            workDir.empty() ? nullptr : workDir.c_str(),
            &si,
            &pi) == FALSE)
        {
            const DWORD launchErr = GetLastError();

            // Roll back: move the new exe back to dribtard.new.exe and restore dribtard.old.exe.
            MoveFileExW(
                currentExePath.c_str(),
                newExePath.c_str(),
                MOVEFILE_REPLACE_EXISTING);
            MoveFileExW(
                oldExePath.c_str(),
                currentExePath.c_str(),
                MOVEFILE_REPLACE_EXISTING);

            outError = "Failed to launch updated executable (" +
                std::to_string(launchErr) + ")";
            Logger::Error(outError);
            return false;
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    Logger::Info("Update applied successfully.");
    return true;
}

void Updater::BackgroundThreadMain()
{
    int secondsSinceLastCheck = 0;

    while (!m_BackgroundStop.load())
    {
        std::string keyToUse;
        bool shouldCheck = false;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);

            if (!m_LicenseKey.empty() &&
                (m_CheckRequested || secondsSinceLastCheck >= kCheckIntervalSeconds))
            {
                keyToUse = m_LicenseKey;
                m_CheckRequested = false;
                shouldCheck = true;
            }
        }

        if (shouldCheck)
        {
            secondsSinceLastCheck = 0;
            CheckNow(keyToUse);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++secondsSinceLastCheck;
    }
}

void Updater::DownloadThreadMain()
{
    DownloadAndVerifyNow();
    m_Downloading.store(false);
}

std::string Updater::ExeDirectory()
{
    const std::wstring exePath = RunningExePathW();

    if (exePath.empty())
        return ".";

    const std::size_t slash = exePath.find_last_of(L"\\/");

    if (slash == std::wstring::npos)
        return ".";

    return WideToUtf8(exePath.substr(0, slash));
}

std::wstring Updater::RunningExePathW()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    if (len == 0 || len >= MAX_PATH)
        return std::wstring();

    return std::wstring(buffer, len);
}

} // namespace Update
