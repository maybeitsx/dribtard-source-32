#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "Licensing/LicenseClient.hpp"
#include "Version.hpp"

namespace Update
{

// Maximum allowed update binary size: 100 MB.
inline constexpr std::uint64_t kMaxUpdateSizeBytes = 100ULL * 1024ULL * 1024ULL;

// ---------------------------------------------------------------------------
// Semantic version comparison & manifest structures (pure logic, unit tested).
// ---------------------------------------------------------------------------

struct SemVer
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// Parses "X.Y.Z" (or "vX.Y.Z"). Rejects non-numeric, negative, or trailing
// garbage.
bool ParseSemVer(
    const std::string& version,
    SemVer& out);

// Returns > 0 if a > b, < 0 if a < b, 0 if equal or invalid.
int CompareSemVer(
    const std::string& a,
    const std::string& b);

// Returns true iff candidateVersion is a valid SemVer strictly greater than
// currentVersion.
bool IsNewerVersion(
    const std::string& candidateVersion,
    const std::string& currentVersion);

struct UpdateManifest
{
    std::string version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
    std::string notes;
    std::string releaseSig;
};

struct UpdateCheckResponse
{
    std::string status;     // "ok", "current", or "invalid"
    std::string reason;     // set when status == "invalid"
    UpdateManifest manifest;
    std::string nonce;
    long long ts = 0;
    std::string sigHex;
};

bool ParseUpdateResponse(
    const std::string& body,
    UpdateCheckResponse& out);

// Exact string the Worker signs with SIGNING_PRIVATE_KEY when an update is available:
//   "update|" + version + "|" + url + "|" + sha256 + "|" + nonce + "|" + ts
std::string BuildUpdateSignedMessage(
    const std::string& version,
    const std::string& url,
    const std::string& sha256,
    const std::string& nonce,
    long long ts);

// Exact string the Worker signs with SIGNING_PRIVATE_KEY when already current:
//   "current|" + nonce + "|" + ts
std::string BuildCurrentSignedMessage(
    const std::string& nonce,
    long long ts);

// Exact string the offline release tool signs with release_private.pem:
//   "release|" + version + "|" + sha256 + "|" + size
std::string BuildReleaseSignedMessage(
    const std::string& version,
    const std::string& sha256,
    std::uint64_t size);

// Verifies the /update response in exact order:
//   1. Server public key is configured (non-zero)
//   2. nonce matches expectedNonce and ts is within the 5-minute clock skew window
//   3. Server Ed25519 signature over "update|..." or "current|..." verifies
//   4. When status == "ok":
//      - manifest.version is strictly greater than currentVersion (semantic compare; rejects downgrades)
//      - manifest.url starts with "https://"
//      - manifest.size > 0 and <= 100 MB, and manifest.sha256 is 64 hex chars
// Sets outHasUpdate = true when status == "ok" and all checks pass;
// sets outHasUpdate = false when status == "current" and signature/nonce/ts pass.
bool VerifyUpdateCheckResponse(
    const UpdateCheckResponse& response,
    const std::string& expectedNonce,
    const std::string& currentVersion,
    const std::array<std::uint8_t, 32>& serverPublicKey,
    long long nowUnixSeconds,
    bool& outHasUpdate,
    std::string& outReason);

// Computes SHA-256 (lowercase hex, 64 chars) via Windows CNG (BCrypt).
bool ComputeSha256Hex(
    const std::uint8_t* data,
    std::size_t size,
    std::string& outHex);

bool ComputeFileSha256Hex(
    const std::string& filePath,
    std::uint64_t& outSize,
    std::string& outHex);

// Verifies a downloaded binary on disk against the manifest and the embedded
// release public key:
//   1. File size matches manifest.size (and <= 100 MB)
//   2. File SHA-256 matches manifest.sha256 (case-insensitive hex)
//   3. manifest.releaseSig verifies with Monocypher Ed25519 against
//      releasePublicKey over "release|" + version + "|" + sha256 + "|" + size
bool VerifyDownloadedPackage(
    const std::string& filePath,
    const UpdateManifest& manifest,
    const std::array<std::uint8_t, 32>& releasePublicKey,
    std::string& outReason);

// ---------------------------------------------------------------------------
// Transport boundary (injectable for unit tests so no network is touched).
// ---------------------------------------------------------------------------

using ProgressCallback = std::function<void(std::uint64_t bytesRead, std::uint64_t totalBytes)>;

class IUpdateTransport
{
public:
    virtual ~IUpdateTransport() = default;

    // POSTs JSON to serverUrl + "/update".
    virtual Licensing::HttpResult PostUpdate(
        const std::string& serverUrl,
        const std::string& body) = 0;

    // Downloads url (HTTPS only, follows redirects, caps at maxBytes) to destPath.
    virtual bool DownloadFile(
        const std::string& url,
        const std::string& destPath,
        std::uint64_t maxBytes,
        ProgressCallback onProgress,
        std::string& outError) = 0;
};

class WinHttpUpdateTransport : public IUpdateTransport
{
public:
    Licensing::HttpResult PostUpdate(
        const std::string& serverUrl,
        const std::string& body) override;

    bool DownloadFile(
        const std::string& url,
        const std::string& destPath,
        std::uint64_t maxBytes,
        ProgressCallback onProgress,
        std::string& outError) override;
};

inline std::unique_ptr<IUpdateTransport> MakeDefaultUpdateTransport()
{
    return std::make_unique<WinHttpUpdateTransport>();
}

// ---------------------------------------------------------------------------
//Updater state and lifecycle
// ---------------------------------------------------------------------------

enum class UpdateStatus
{
    Idle,
    Checking,
    UpToDate,
    UpdateAvailable,
    Downloading,
    ReadyToApply,
    Failed,
};

struct UpdateState
{
    UpdateStatus status = UpdateStatus::Idle;
    UpdateManifest manifest;
    float progress = 0.0f; // 0.0 .. 1.0
    std::uint64_t downloadedBytes = 0;
    std::uint64_t totalBytes = 0;
    std::string statusText;
    std::string errorReason;
    std::string stagedPath;
};

class Updater
{
public:
    Updater(
        std::string serverUrl,
        std::array<std::uint8_t, 32> serverPublicKey,
        std::array<std::uint8_t, 32> releasePublicKey,
        std::string currentVersion = Version::kAppVersion,
        std::unique_ptr<IUpdateTransport> transport = MakeDefaultUpdateTransport());

    ~Updater();

    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    // Startup cleanup: deletes dribtard.old.exe and dribtard.new.exe in the
    // running executable's directory if present.
    static void CleanupOldFiles();
    static void CleanupOldFilesInDir(const std::string& exeDir);

    // Synchronous check (call from a worker thread or test).
    UpdateState CheckNow(
        const std::string& licenseKey,
        const std::string& hwid = Licensing::ComputeHwid());

    // Synchronous download + cryptographic verification (call from a worker
    // thread or test). Writes to <exeDir>\dribtard.new.exe.
    UpdateState DownloadAndVerifyNow(
        const std::string& destDirectory = "");

    // Starts the periodic background check loop (runs an immediate check once
    // notified of a valid license key, and then every few hours).
    void StartBackgroundLoop();
    void StopBackgroundLoop();

    // Notifies the updater that the license was validated with `licenseKey`
    // and triggers an asynchronous check if none is in flight.
    void NotifyLicenseValid(const std::string& licenseKey);

    // Starts an asynchronous download + verification of the available update
    // on a worker thread. Never blocks the render loop.
    void StartDownloadAsync();

    UpdateState CurrentState() const;

    // Applies the verified update in <exeDir>\dribtard.new.exe:
    //   1. Renames the running exe to dribtard.old.exe
    //   2. Moves dribtard.new.exe into the original exe name
    //   3. Launches the new exe with CreateProcessW
    //   4. Returns true so the caller can exit the current process cleanly
    // If anything fails after the rename, renames dribtard.old.exe back.
    bool ApplyAndRestart(std::string& outError);

    static bool ApplyStagedUpdateForPaths(
        const std::wstring& currentExePath,
        const std::wstring& newExePath,
        const std::wstring& oldExePath,
        bool launchNewProcess,
        std::string& outError);

private:
    void BackgroundThreadMain();
    void DownloadThreadMain();

    static std::string ExeDirectory();
    static std::wstring RunningExePathW();

    std::string m_ServerUrl;
    std::array<std::uint8_t, 32> m_ServerPublicKey;
    std::array<std::uint8_t, 32> m_ReleasePublicKey;
    std::string m_CurrentVersion;
    std::unique_ptr<IUpdateTransport> m_Transport;

    mutable std::mutex m_Mutex;
    UpdateState m_State;
    std::string m_LicenseKey;
    bool m_CheckRequested = false;

    std::thread m_BackgroundThread;
    std::atomic<bool> m_BackgroundStop{ false };

    std::thread m_DownloadThread;
    std::atomic<bool> m_Downloading{ false };
};

} // namespace Update
