#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

namespace Licensing
{

// ---------------------------------------------------------------------------
// Small helpers (pure logic, no network - unit tested).
// ---------------------------------------------------------------------------

struct ValidateResponse
{
    std::string status;
    std::string reason;
    std::string key;
    std::string hwid;
    std::string nonce;
    long long ts = 0;
    long long expiresAt = 0;
    std::string sigHex;
};

// Minimal flat-JSON parser for the server's validation responses.
// Returns false when the body is not valid JSON or lacks "status".
bool ParseValidateResponse(
    const std::string& body,
    ValidateResponse& out);

// Exact string the server signs:
//   "valid|" + key + "|" + hwid + "|" + nonce + "|" + ts + "|" + expiresAt
std::string BuildSignedMessage(
    const std::string& key,
    const std::string& hwid,
    const std::string& nonce,
    long long ts,
    long long expiresAt);

bool HexToBytes(
    const std::string& hex,
    std::uint8_t* out,
    std::size_t outSize);

std::string BytesToHex(
    const std::uint8_t* data,
    std::size_t size);

// Ed25519 verification via Monocypher (crypto_ed25519_check).
bool VerifyEd25519(
    const std::array<std::uint8_t, 32>& publicKey,
    const std::string& message,
    const std::array<std::uint8_t, 64>& signature);

// Full client-side check: status == valid, nonce/hwid match what we sent,
// ts within 5 minutes of now, and the Ed25519 signature verifies.
// On failure returns false and sets outReason.
bool VerifyLicenseResponse(
    const ValidateResponse& response,
    const std::string& expectedKey,
    const std::string& expectedHwid,
    const std::string& expectedNonce,
    const std::array<std::uint8_t, 32>& serverPublicKey,
    long long nowUnixSeconds,
    std::string& outReason,
    long long& outExpiresAt);

// Random hex nonce (16 bytes by default) for replay protection.
std::string GenerateNonceHex(
    std::size_t byteCount = 16);

// Current Unix time in seconds.
long long NowUnixSeconds();

// JSON-escape a string value.
std::string JsonEscape(
    const std::string& value);

// ---------------------------------------------------------------------------
// Hardware ID (Windows only). Stable per machine, never sends raw identifiers:
// MachineGuid + system volume serial, hashed with BLAKE2b, hex-encoded.
// ---------------------------------------------------------------------------

std::string ComputeHwid();

// ---------------------------------------------------------------------------
// HTTP transport boundary (injectable so tests use a fake, no network).
// ---------------------------------------------------------------------------

struct HttpResult
{
    bool ok = false;
    std::string body;
    std::string error;
};

// Formats a failed WinHTTP call with its numeric code and system message.
std::string WinHttpErrorText(
    const char* operation,
    unsigned long code);

// Performs a plain HTTPS POST to serverUrl + endpointPath (e.g. "/validate" or "/update").
HttpResult WinHttpPostJson(
    const std::string& serverUrl,
    const std::string& endpointPath,
    const std::string& body);

class IValidateTransport
{
public:
    virtual ~IValidateTransport() = default;

    // POSTs body to serverUrl + "/validate". Plain HTTPS call, 10s timeout.
    virtual HttpResult PostValidate(
        const std::string& serverUrl,
        const std::string& body) = 0;
};

// Production WinHTTP transport (Windows only, HTTPS only).
class WinHttpTransport : public IValidateTransport
{
public:
    HttpResult PostValidate(
        const std::string& serverUrl,
        const std::string& body) override;
};

inline std::unique_ptr<IValidateTransport> MakeDefaultTransport()
{
    return std::make_unique<WinHttpTransport>();
}

// ---------------------------------------------------------------------------
// License state shared with the UI.
// ---------------------------------------------------------------------------

enum class LicenseStatus
{
    Unknown,
    Checking,
    Valid,
    Invalid,
    Offline, // valid cached response, network unreachable
};

struct LicenseState
{
    LicenseStatus status = LicenseStatus::Unknown;
    std::string reason;     // machine-readable, e.g. "hwid_mismatch"
    std::string statusText; // human-readable status line for the UI
    std::string expiryText; // human-readable expiry info once valid
    long long expiresAt = 0; // 0 = lifetime
};

// ---------------------------------------------------------------------------
// LicenseManager: validates, persists the key, caches the last good signed
// response for up to 24h offline use, and re-validates every 15 minutes on a
// worker thread. Network never runs on the UI thread.
// ---------------------------------------------------------------------------

class LicenseManager
{
public:
    using InvalidCallback = std::function<void(const std::string& reason)>;

    LicenseManager(
        std::string serverUrl,
        std::array<std::uint8_t, 32> serverPublicKey,
        std::unique_ptr<IValidateTransport> transport = MakeDefaultTransport());

    ~LicenseManager();

    LicenseManager(const LicenseManager&) = delete;
    LicenseManager& operator=(const LicenseManager&) = delete;

    // Loads %APPDATA%\dribtard\license.dat (plain file) into memory.
    // Returns true when a saved key was found.
    bool LoadSavedKey();

    void SaveKey(
        const std::string& key);

    void ClearKey();

    std::string SavedKey() const;

    // Synchronous online validation (call from a worker thread, not the UI).
    // Falls back to the cached signed response when the network fails.
    LicenseState ValidateNow(
        const std::string& key);

    // Re-verified cached response only (no network). Used at startup before
    // the first online check completes.
    LicenseState LoadCachedState();

    LicenseState CurrentState() const;
    bool IsValid() const;

    // Starts the 15-minute background re-validation loop. onInvalid is
    // invoked (from the worker thread) when the license becomes invalid.
    void StartBackgroundLoop(
        InvalidCallback onInvalid);

    void StopBackgroundLoop();

private:
    LicenseState ValidateOnline(
        const std::string& key,
        const std::string& hwid);

    LicenseState ValidateOfflineCache(
        const std::string& expectedKey,
        const std::string& expectedHwid);

    void BackgroundThreadMain();

    static std::string DataDir();
    static std::string KeyFilePath();
    static std::string CacheFilePath();
    static bool WriteTextFile(
        const std::string& path,
        const std::string& content);
    static bool ReadTextFile(
        const std::string& path,
        std::string& content);

    std::string m_ServerUrl;
    std::array<std::uint8_t, 32> m_ServerPublicKey;
    std::unique_ptr<IValidateTransport> m_Transport;

    mutable std::mutex m_Mutex;
    std::string m_SavedKey;
    LicenseState m_State;

    std::thread m_BackgroundThread;
    std::atomic<bool> m_BackgroundStop{ false };
    InvalidCallback m_OnInvalid;
};

} // namespace Licensing
