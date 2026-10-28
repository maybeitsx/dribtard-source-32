#include "Licensing/LicenseClient.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>

#include "Logging/Logger.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <winhttp.h>

#include "monocypher.h"
#include "monocypher-ed25519.h"

namespace Licensing
{
namespace
{

constexpr long long kMaxClockSkewSeconds = 5 * 60; // ts must be within 5 minutes
constexpr long long kOfflineCacheSeconds = 24 * 60 * 60; // cached response good for 24h
constexpr int kHttpTimeoutMs = 10000;

// --- Minimal flat-JSON field extraction -----------------------------------
// Only handles the server's simple response shape: string values in quotes,
// integers, and null. Enough for validation responses without a JSON library.

const char* SkipWhitespace(
    const char* p,
    const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;

    return p;
}

// Finds the raw value span for "name": <value> at the top level of object.
bool FindFieldValue(
    const std::string& body,
    const std::string& name,
    const char*& valueBegin,
    const char*& valueEnd)
{
    const std::string quoted = "\"" + name + "\"";
    const char* begin = body.data();
    const char* end = begin + body.size();

    const char* keyPos = std::search(
        begin, end,
        quoted.data(), quoted.data() + quoted.size());

    if (keyPos == end)
        return false;

    const char* p = SkipWhitespace(keyPos + quoted.size(), end);

    if (p == end || *p != ':')
        return false;

    p = SkipWhitespace(p + 1, end);

    if (p == end)
        return false;

    if (*p == '"')
    {
        // Quoted string with minimal unescaping.
        ++p;
        std::string unescaped;
        bool escaped = false;

        const char* q = p;

        while (q < end)
        {
            if (escaped)
            {
                switch (*q)
                {
                case '"': unescaped += '"'; break;
                case '\\': unescaped += '\\'; break;
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
            else if (*q == '"')
            {
                break;
            }
            else
            {
                unescaped += *q;
            }

            ++q;
        }

        if (q == end)
            return false;

        // Store into a thread-local scratch buffer owned by the caller below.
        // We return the span trick via static storage per call instead:
        // simpler to stash the parsed string in a static and return its span.
        static thread_local std::string scratch;
        scratch = unescaped;
        valueBegin = scratch.data();
        valueEnd = scratch.data() + scratch.size();
        return true;
    }

    // Bare token: number, true/false/null.
    const char* q = p;

    while (q < end && *q != ',' && *q != '}')
        ++q;

    // Trim trailing whitespace.
    const char* r = q;

    while (r > p && (r[-1] == ' ' || r[-1] == '\t' || r[-1] == '\n' || r[-1] == '\r'))
        --r;

    valueBegin = p;
    valueEnd = r;
    return true;
}

bool GetJsonString(
    const std::string& body,
    const std::string& name,
    std::string& out)
{
    const char* begin = nullptr;
    const char* end = nullptr;

    if (!FindFieldValue(body, name, begin, end))
        return false;

    out.assign(begin, end);
    return true;
}

bool GetJsonInt64(
    const std::string& body,
    const std::string& name,
    long long& out)
{
    const char* begin = nullptr;
    const char* end = nullptr;

    if (!FindFieldValue(body, name, begin, end))
        return false;

    if (end - begin == 4 && std::string(begin, end) == "null")
    {
        out = 0;
        return true;
    }

    try
    {
        out = std::stoll(std::string(begin, end));
        return true;
    }
    catch (...)
    {
        return false;
    }
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

std::string FormatExpiryText(
    long long expiresAt)
{
    if (expiresAt == 0)
        return "License: lifetime";

    const std::time_t t = static_cast<std::time_t>(expiresAt);
    std::tm tmValue{};

    if (gmtime_s(&tmValue, &t) != 0)
        return "License: expires soon";

    char buffer[64]{};
    std::snprintf(
        buffer, sizeof(buffer),
        "License: expires %04d-%02d-%02d",
        tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday);

    return std::string(buffer);
}

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

} // namespace

bool ParseValidateResponse(
    const std::string& body,
    ValidateResponse& out)
{
    out = ValidateResponse{};

    if (!GetJsonString(body, "status", out.status))
        return false;

    GetJsonString(body, "reason", out.reason);
    GetJsonString(body, "key", out.key);
    GetJsonString(body, "hwid", out.hwid);
    GetJsonString(body, "nonce", out.nonce);
    GetJsonString(body, "sig", out.sigHex);
    GetJsonInt64(body, "ts", out.ts);
    GetJsonInt64(body, "expiresAt", out.expiresAt);

    return !out.status.empty();
}

std::string BuildSignedMessage(
    const std::string& key,
    const std::string& hwid,
    const std::string& nonce,
    long long ts,
    long long expiresAt)
{
    return "valid|" + key + "|" + hwid + "|" + nonce + "|" +
        std::to_string(ts) + "|" + std::to_string(expiresAt);
}

bool HexToBytes(
    const std::string& hex,
    std::uint8_t* out,
    std::size_t outSize)
{
    if (hex.size() != outSize * 2 || outSize == 0)
        return false;

    for (std::size_t i = 0; i < outSize; ++i)
    {
        const char hi = hex[i * 2];
        const char lo = hex[i * 2 + 1];

        if (!std::isxdigit(static_cast<unsigned char>(hi)) ||
            !std::isxdigit(static_cast<unsigned char>(lo)))
        {
            return false;
        }

        const auto nibble = [](char c) -> unsigned
        {
            if (c >= '0' && c <= '9')
                return static_cast<unsigned>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<unsigned>(c - 'a' + 10);
            return static_cast<unsigned>(c - 'A' + 10);
        };

        out[i] = static_cast<std::uint8_t>((nibble(hi) << 4) | nibble(lo));
    }

    return true;
}

std::string BytesToHex(
    const std::uint8_t* data,
    std::size_t size)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);

    for (std::size_t i = 0; i < size; ++i)
    {
        out += kDigits[(data[i] >> 4) & 0xF];
        out += kDigits[data[i] & 0xF];
    }

    return out;
}

bool VerifyEd25519(
    const std::array<std::uint8_t, 32>& publicKey,
    const std::string& message,
    const std::array<std::uint8_t, 64>& signature)
{
    const int result = crypto_ed25519_check(
        signature.data(),
        publicKey.data(),
        reinterpret_cast<const std::uint8_t*>(message.data()),
        message.size());

    return result == 0;
}

bool VerifyLicenseResponse(
    const ValidateResponse& response,
    const std::string& expectedKey,
    const std::string& expectedHwid,
    const std::string& expectedNonce,
    const std::array<std::uint8_t, 32>& serverPublicKey,
    long long nowUnixSeconds,
    std::string& outReason,
    long long& outExpiresAt)
{
    outReason.clear();
    outExpiresAt = 0;

    if (IsAllZeros(serverPublicKey))
    {
        outReason = "not_configured";
        return false;
    }

    if (response.status != "valid")
    {
        outReason = response.reason.empty() ? "invalid" : response.reason;
        return false;
    }

    if (ToUpperTrimmed(response.key) != ToUpperTrimmed(expectedKey))
    {
        outReason = "key_mismatch";
        return false;
    }

    if (response.hwid != expectedHwid)
    {
        outReason = "hwid_mismatch";
        return false;
    }

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

    if (response.expiresAt != 0 && nowUnixSeconds > response.expiresAt)
    {
        outReason = "expired";
        return false;
    }

    std::array<std::uint8_t, 64> signature{};

    if (!HexToBytes(response.sigHex, signature.data(), signature.size()))
    {
        outReason = "bad_signature";
        return false;
    }

    const std::string message = BuildSignedMessage(
        ToUpperTrimmed(response.key),
        response.hwid,
        response.nonce,
        response.ts,
        response.expiresAt);

    if (!VerifyEd25519(serverPublicKey, message, signature))
    {
        outReason = "bad_signature";
        return false;
    }

    outExpiresAt = response.expiresAt;
    return true;
}

std::string GenerateNonceHex(
    std::size_t byteCount)
{
    std::string bytes(byteCount, '\0');

    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (status < 0)
    {
        // Fallback: not cryptographic, but keeps the flow working.
        // The server signature still protects the response.
        std::srand(static_cast<unsigned>(std::time(nullptr)));

        for (auto& c : bytes)
            c = static_cast<char>(std::rand() % 256);
    }

    return BytesToHex(
        reinterpret_cast<const std::uint8_t*>(bytes.data()),
        bytes.size());
}

long long NowUnixSeconds()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

std::string JsonEscape(
    const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 2);

    for (const char c : value)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }

    return out;
}

std::string ComputeHwid()
{
    // MachineGuid from the registry (stable per Windows install).
    std::string machineGuid;
    {
        HKEY key = nullptr;

        if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Cryptography",
            0,
            KEY_READ | KEY_WOW64_64KEY,
            &key) == ERROR_SUCCESS)
        {
            wchar_t buffer[256]{};
            DWORD sizeBytes = sizeof(buffer);
            DWORD type = 0;

            if (RegQueryValueExW(
                key,
                L"MachineGuid",
                nullptr,
                &type,
                reinterpret_cast<LPBYTE>(buffer),
                &sizeBytes) == ERROR_SUCCESS &&
                type == REG_SZ)
            {
                char narrow[256]{};
                WideCharToMultiByte(
                    CP_UTF8, 0,
                    buffer, -1,
                    narrow, static_cast<int>(sizeof(narrow)),
                    nullptr, nullptr);
                machineGuid = narrow;
            }

            RegCloseKey(key);
        }
    }

    // System volume serial number (stable per disk/format).
    std::string volumeSerial;
    {
        wchar_t systemDir[MAX_PATH]{};

        if (GetSystemDirectoryW(systemDir, MAX_PATH) > 0)
        {
            wchar_t root[4] = { systemDir[0], L':', L'\\', L'\0' };
            DWORD serial = 0;

            if (GetVolumeInformationW(
                root,
                nullptr, 0,
                &serial,
                nullptr, nullptr,
                nullptr, 0))
            {
                char serialText[16]{};
                std::snprintf(serialText, sizeof(serialText), "%08lX",
                    static_cast<unsigned long>(serial));
                volumeSerial = serialText;
            }
        }
    }

    // Never send raw identifiers: hash the combination with BLAKE2b.
    const std::string combined = machineGuid + "|" + volumeSerial;
    std::array<std::uint8_t, 32> hash{};

    crypto_blake2b(
        hash.data(), hash.size(),
        reinterpret_cast<const std::uint8_t*>(combined.data()),
        combined.size());

    return BytesToHex(hash.data(), hash.size());
}

// Formats a failed WinHTTP call with its numeric code and system message,
// e.g. "WinHttpSendRequest failed (12007): The server name could not be
// resolved." The code is captured before any handle is closed so it stays
// accurate; without it these failures are undebuggable.
std::string WinHttpErrorText(
    const char* operation,
    unsigned long code)
{
    std::string text = std::string(operation) +
        " failed (" + std::to_string(code) + ")";

    char* buffer = nullptr;

    if (FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_HMODULE |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        GetModuleHandleW(L"winhttp.dll"),
        static_cast<DWORD>(code),
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr) != 0 &&
        buffer != nullptr)
    {
        std::string message = buffer;
        LocalFree(buffer);

        while (!message.empty() &&
            std::isspace(static_cast<unsigned char>(message.back())))
        {
            message.pop_back();
        }

        if (!message.empty())
            text += ": " + message;
    }

    return text;
}

static HttpResult PerformPost(
    const std::string& host,
    const std::string& path,
    const std::string& body,
    DWORD accessType);

HttpResult WinHttpPostJson(
    const std::string& serverUrl,
    const std::string& endpointPath,
    const std::string& body)
{
    HttpResult result;

    // HTTPS only: refuse plain http and non-http(s) URLs.
    if (serverUrl.rfind("https://", 0) != 0)
    {
        result.error = "Server URL must use https://";
        return result;
    }

    const std::string endpoint =
        (!endpointPath.empty() && endpointPath.front() == '/')
        ? endpointPath
        : ("/" + endpointPath);

    // Tolerate a trailing slash or whitespace in the configured URL so it
    // cannot produce a "//validate" path (which the server answers with 404).
    std::string normalizedUrl = serverUrl;

    while (normalizedUrl.size() > 8 &&
        (normalizedUrl.back() == '/' ||
            std::isspace(static_cast<unsigned char>(normalizedUrl.back()))))
    {
        normalizedUrl.pop_back();
    }

    std::string host;
    std::string path = endpoint;
    {
        std::string rest = normalizedUrl.substr(8);
        const std::size_t slash = rest.find('/');

        if (slash != std::string::npos)
        {
            host = rest.substr(0, slash);
            path = rest.substr(slash);

            if (path.size() < endpoint.size() ||
                path.compare(path.size() - endpoint.size(), endpoint.size(), endpoint) != 0)
            {
                path += endpoint;
            }
        }
        else
        {
            host = rest;
        }
    }

    if (host.empty())
    {
        result.error = "Invalid server URL";
        return result;
    }

    // Try the system proxy configuration first, then fall back to a direct
    // connection: on some machines (VPNs, custom setups, Cloudflare WARP)
    // one path works while the other fails at send time. Both attempts are
    // plain, visible HTTPS with the same timeout.
    HttpResult first = PerformPost(
        host,
        path,
        body,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY);

    if (first.ok || !first.body.empty())
        return first;

    HttpResult direct = PerformPost(
        host,
        path,
        body,
        WINHTTP_ACCESS_TYPE_NO_PROXY);

    if (!direct.ok && direct.body.empty())
        direct.error += " [system proxy attempt: " + first.error + "]";

    return direct;
}

HttpResult WinHttpTransport::PostValidate(
    const std::string& serverUrl,
    const std::string& body)
{
    return WinHttpPostJson(serverUrl, "/validate", body);
}

// One HTTPS POST attempt. Transport failures carry the Win32 error code so
// the caller can report the real cause.
static HttpResult PerformPost(
    const std::string& host,
    const std::string& path,
    const std::string& body,
    DWORD accessType)
{
    HttpResult result;

    std::wstring wideHost(host.begin(), host.end());
    std::wstring widePath(path.begin(), path.end());

    HINTERNET session = WinHttpOpen(
        L"ExternalMacro/1.0",
        accessType,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (session == nullptr)
    {
        result.error = WinHttpErrorText("WinHttpOpen", GetLastError());
        return result;
    }

    // Plain, visible HTTPS request with a sane 10s timeout.
    WinHttpSetTimeouts(session, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs);

    HINTERNET connection = WinHttpConnect(
        session,
        wideHost.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT,
        0);

    if (connection == nullptr)
    {
        result.error = WinHttpErrorText("WinHttpConnect", GetLastError());
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        widePath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);

    if (request == nullptr)
    {
        result.error = WinHttpErrorText("WinHttpOpenRequest", GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    const std::wstring headers = L"Content-Type: application/json\r\n";
    const BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);

    if (sent == FALSE)
    {
        result.error = WinHttpErrorText("WinHttpSendRequest", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    if (WinHttpReceiveResponse(request, nullptr) == FALSE)
    {
        result.error = WinHttpErrorText("WinHttpReceiveResponse", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
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

    std::string responseBody;
    DWORD available = 0;

    while (WinHttpQueryDataAvailable(request, &available) == TRUE && available > 0)
    {
        std::string chunk(static_cast<std::size_t>(available), '\0');
        DWORD downloaded = 0;

        if (WinHttpReadData(
            request,
            chunk.data(),
            available,
            &downloaded) == FALSE)
        {
            break;
        }

        chunk.resize(downloaded);
        responseBody += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (statusCode == 429)
    {
        result.error = "rate_limited";
        return result;
    }

    if (statusCode < 200 || statusCode >= 300)
    {
        result.error = "HTTP " + std::to_string(statusCode);
        result.body = responseBody;
        return result;
    }

    result.ok = true;
    result.body = responseBody;
    return result;
}

LicenseManager::LicenseManager(
    std::string serverUrl,
    std::array<std::uint8_t, 32> serverPublicKey,
    std::unique_ptr<IValidateTransport> transport)
    : m_ServerUrl(std::move(serverUrl))
    , m_ServerPublicKey(serverPublicKey)
    , m_Transport(std::move(transport))
{
    m_State.status = LicenseStatus::Unknown;
    m_State.statusText = "Enter your license key.";
}

LicenseManager::~LicenseManager()
{
    StopBackgroundLoop();
}

bool LicenseManager::LoadSavedKey()
{
    std::string content;

    if (!ReadTextFile(KeyFilePath(), content))
        return false;

    const std::string key = ToUpperTrimmed(content);

    if (key.empty())
        return false;

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_SavedKey = key;
    return true;
}

void LicenseManager::SaveKey(
    const std::string& key)
{
    const std::string normalized = ToUpperTrimmed(key);

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_SavedKey = normalized;
    }

    if (!WriteTextFile(KeyFilePath(), normalized))
    {
        Logger::Error(
            "Could not save the license key file.");
    }
}

void LicenseManager::ClearKey()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_SavedKey.clear();
        m_State = LicenseState{};
        m_State.statusText = "Enter your license key.";
    }

    DeleteFileA(KeyFilePath().c_str());
}

std::string LicenseManager::SavedKey() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_SavedKey;
}

LicenseState LicenseManager::ValidateNow(
    const std::string& key)
{
    const std::string normalized = ToUpperTrimmed(key);

    if (normalized.empty())
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "empty_key";
        state.statusText = "Enter your license key.";

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State = state;
        return state;
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State.status = LicenseStatus::Checking;
        m_State.statusText = "Checking license...";
    }

    const std::string hwid = ComputeHwid();
    LicenseState online = ValidateOnline(normalized, hwid);

    if (online.status == LicenseStatus::Valid)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_State = online;
        return online;
    }

    // No verdict from the server (network, HTTP or rate-limit failure): try
    // the cached signed response. When the cache is unusable, report the
    // ONLINE failure - never a cache-miss message - so the UI shows the
    // real, actionable reason.
    if (online.reason == "network_error" ||
        online.reason == "http_error" ||
        online.reason == "rate_limited" ||
        online.reason == "bad_response")
    {
        LicenseState offline = ValidateOfflineCache(normalized, hwid);

        if (offline.status == LicenseStatus::Offline)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_State = offline;
            return offline;
        }
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_State = online;
    return online;
}

LicenseState LicenseManager::LoadCachedState()
{
    const std::string key = SavedKey();

    if (key.empty())
    {
        LicenseState state;
        state.statusText = "Enter your license key.";
        return state;
    }

    return ValidateOfflineCache(key, ComputeHwid());
}

LicenseState LicenseManager::CurrentState() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_State;
}

bool LicenseManager::IsValid() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_State.status == LicenseStatus::Valid ||
        m_State.status == LicenseStatus::Offline;
}

void LicenseManager::StartBackgroundLoop(
    InvalidCallback onInvalid)
{
    StopBackgroundLoop();

    m_OnInvalid = std::move(onInvalid);
    m_BackgroundStop.store(false);
    m_BackgroundThread = std::thread(&LicenseManager::BackgroundThreadMain, this);
}

void LicenseManager::StopBackgroundLoop()
{
    m_BackgroundStop.store(true);

    if (m_BackgroundThread.joinable())
        m_BackgroundThread.join();
}

LicenseState LicenseManager::ValidateOnline(
    const std::string& key,
    const std::string& hwid)
{
    const std::string nonce = GenerateNonceHex();
    const std::string requestBody =
        "{\"key\":\"" + JsonEscape(key) +
        "\",\"hwid\":\"" + JsonEscape(hwid) +
        "\",\"nonce\":\"" + JsonEscape(nonce) + "\"}";

    HttpResult http;

    try
    {
        http = m_Transport->PostValidate(m_ServerUrl, requestBody);
    }
    catch (...)
    {
        http.ok = false;
        http.error = "transport exception";
    }

    if (!http.ok)
    {
        // No verdict from the server: the caller may fall back to the offline
        // cache. A server "invalid" verdict still arrives as HTTP 200 JSON.
        if (!http.body.empty())
        {
            ValidateResponse parsed;

            if (ParseValidateResponse(http.body, parsed) && parsed.status == "invalid")
            {
                LicenseState state;
                state.status = LicenseStatus::Invalid;
                state.reason = parsed.reason.empty() ? "invalid" : parsed.reason;
                state.statusText = "License invalid: " + state.reason;

                Logger::Info(
                    "License check failed: " + state.reason);

                return state;
            }
        }

        // Keep the true failure visible: rate limits and HTTP errors get
        // their own reason so the UI never blames the offline cache.
        LicenseState state;
        state.status = LicenseStatus::Invalid;

        if (http.error == "rate_limited")
        {
            state.reason = "rate_limited";
            state.statusText = "License server is rate-limiting this machine. Try again shortly.";
        }
        else if (http.error.rfind("HTTP ", 0) == 0)
        {
            state.reason = "http_error";
            state.statusText = "License server replied with " + http.error + ". Check the server URL.";
        }
        else
        {
            state.reason = "network_error";
            state.statusText = "Could not reach the license server (" + http.error + ").";
        }

        Logger::Error(
            "License check failed: " + http.error);

        return state;
    }

    ValidateResponse response;

    if (!ParseValidateResponse(http.body, response))
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "bad_response";
        state.statusText = "License server returned an unreadable response.";
        return state;
    }

    if (response.status != "valid")
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = response.reason.empty() ? "invalid" : response.reason;
        state.statusText = "License invalid: " + state.reason;

        Logger::Info(
            "License check failed: " + state.reason);

        return state;
    }

    std::string reason;
    long long expiresAt = 0;

    if (!VerifyLicenseResponse(
        response,
        key,
        hwid,
        nonce,
        m_ServerPublicKey,
        NowUnixSeconds(),
        reason,
        expiresAt))
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = reason;
        state.statusText = "License invalid: " + reason;

        Logger::Info(
            "License verification failed: " + reason);

        return state;
    }

    // Cache the last good signed response for offline use.
    WriteTextFile(CacheFilePath(), http.body);

    LicenseState state;
    state.status = LicenseStatus::Valid;
    state.expiresAt = expiresAt;
    state.statusText = "License valid.";
    state.expiryText = FormatExpiryText(expiresAt);

    Logger::Info(
        "License validated successfully.");

    return state;
}

LicenseState LicenseManager::ValidateOfflineCache(
    const std::string& expectedKey,
    const std::string& expectedHwid)
{
    std::string cached;

    if (!ReadTextFile(CacheFilePath(), cached))
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "network_error";
        state.statusText = "Could not reach the license server.";
        return state;
    }

    ValidateResponse response;

    if (!ParseValidateResponse(cached, response))
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "network_error";
        state.statusText = "Offline cache unreadable; connect once to activate.";
        return state;
    }

    // Re-verify the cached signature and freshness: the cache proves a past
    // successful check, good for up to 24 hours after its ts.
    const long long now = NowUnixSeconds();

    if (now - response.ts > kOfflineCacheSeconds)
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "offline_expired";
        state.statusText = "Offline license expired; connect to re-validate.";
        return state;
    }

    std::string reason;
    long long expiresAt = 0;

    // The cached nonce is the one we originally sent; accept it as long as
    // the signature verifies and the ts is within the 24h window (the 5-min
    // check is skipped for cached responses by design).
    std::array<std::uint8_t, 64> signature{};

    if (ToUpperTrimmed(response.key) != ToUpperTrimmed(expectedKey) ||
        response.hwid != expectedHwid ||
        !HexToBytes(response.sigHex, signature.data(), signature.size()))
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "network_error";
        state.statusText = "Offline cache does not match this key/machine.";
        return state;
    }

    const std::string message = BuildSignedMessage(
        ToUpperTrimmed(response.key),
        response.hwid,
        response.nonce,
        response.ts,
        response.expiresAt);

    if (IsAllZeros(m_ServerPublicKey) ||
        !VerifyEd25519(m_ServerPublicKey, message, signature))
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "network_error";
        state.statusText = "Offline cache signature invalid.";
        return state;
    }

    if (response.expiresAt != 0 && now > response.expiresAt)
    {
        LicenseState state;
        state.status = LicenseStatus::Invalid;
        state.reason = "expired";
        state.statusText = "License expired.";
        return state;
    }

    expiresAt = response.expiresAt;

    LicenseState state;
    state.status = LicenseStatus::Offline;
    state.expiresAt = expiresAt;
    state.statusText = "Offline license (last verified recently).";
    state.expiryText = FormatExpiryText(expiresAt);

    // Reuse the shared verification for logging consistency.
    (void)reason;

    Logger::Info(
        "Using cached offline license.");

    return state;
}

void LicenseManager::BackgroundThreadMain()
{
    // Re-validate every 15 minutes. The first wait is a full interval so the
    // startup check has time to complete.
    constexpr auto kInterval = std::chrono::minutes(15);

    while (!m_BackgroundStop.load())
    {
        for (int i = 0; i < 15 * 60; ++i)
        {
            if (m_BackgroundStop.load())
                return;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (m_BackgroundStop.load())
            return;

        const std::string key = SavedKey();

        if (key.empty())
            continue;

        const LicenseState state = ValidateNow(key);

        if (!IsValid() && m_OnInvalid)
            m_OnInvalid(state.reason);
    }
}

std::string LicenseManager::DataDir()
{
    // Test override: when set, the license files live under this directory
    // instead of %APPDATA% so unit tests never touch the real machine state.
    char overrideDir[MAX_PATH]{};

    if (GetEnvironmentVariableA(
        "DRIBTARD_LICENSE_DIR",
        overrideDir,
        static_cast<DWORD>(sizeof(overrideDir))) > 0)
    {
        return std::string(overrideDir);
    }

    char appData[MAX_PATH]{};

    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData)))
        return ".";

    return std::string(appData) + "\\dribtard";
}

std::string LicenseManager::KeyFilePath()
{
    return DataDir() + "\\license.dat";
}

std::string LicenseManager::CacheFilePath()
{
    return DataDir() + "\\license_cache.json";
}

bool LicenseManager::WriteTextFile(
    const std::string& path,
    const std::string& content)
{
    const std::size_t slash = path.find_last_of("\\/");

    if (slash != std::string::npos)
        CreateDirectoryA(path.substr(0, slash).c_str(), nullptr);

    HANDLE handle = CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(
        handle,
        content.data(),
        static_cast<DWORD>(content.size()),
        &written,
        nullptr);

    CloseHandle(handle);
    return ok == TRUE && written == content.size();
}

bool LicenseManager::ReadTextFile(
    const std::string& path,
    std::string& content)
{
    HANDLE handle = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE)
        return false;

    std::string out;
    char buffer[4096]{};
    DWORD read = 0;

    while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) == TRUE && read > 0)
        out.append(buffer, read);

    CloseHandle(handle);
    content = out;
    return true;
}

} // namespace Licensing
