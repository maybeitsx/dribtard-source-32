// Unit tests for the auto-update verification, download, and apply logic.
//
// Uses a fake IUpdateTransport so no network is touched. Both keypairs
// (the server keypair and the distinct offline release signing keypair) are
// generated deterministically at runtime with Monocypher.

#include "Update/Updater.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "monocypher.h"
#include "monocypher-ed25519.h"

namespace
{

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct TestKeys
{
    std::array<std::uint8_t, 64> secret{};
    std::array<std::uint8_t, 32> pub{};
};

TestKeys MakeTestKeys(std::uint8_t seedOffset)
{
    std::array<std::uint8_t, 32> seed{};

    for (std::size_t i = 0; i < seed.size(); ++i)
        seed[i] = static_cast<std::uint8_t>(seedOffset + i + 1);

    TestKeys keys;
    crypto_ed25519_key_pair(keys.secret.data(), keys.pub.data(), seed.data());
    return keys;
}

std::string SignHex(
    const TestKeys& keys,
    const std::string& message)
{
    std::array<std::uint8_t, 64> sig{};
    crypto_ed25519_sign(
        sig.data(),
        keys.secret.data(),
        reinterpret_cast<const std::uint8_t*>(message.data()),
        message.size());

    return Licensing::BytesToHex(sig.data(), sig.size());
}

bool WriteRawFile(
    const std::string& path,
    const std::string& data)
{
    HANDLE file = CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(
        file,
        data.data(),
        static_cast<DWORD>(data.size()),
        &written,
        nullptr);

    CloseHandle(file);
    return ok == TRUE && written == data.size();
}

std::string ReadRawFile(
    const std::string& path)
{
    HANDLE file = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return "";

    std::string out;
    char buf[4096]{};
    DWORD read = 0;

    while (ReadFile(file, buf, sizeof(buf), &read, nullptr) == TRUE && read > 0)
        out.append(buf, read);

    CloseHandle(file);
    return out;
}

struct UpdateFixture
{
    TestKeys serverKeys = MakeTestKeys(10);
    TestKeys releaseKeys = MakeTestKeys(90);

    std::string currentVersion = "1.0.0";
    std::string newVersion = "1.3.0";
    std::string url = "https://github.com/example/dribtard-releases/releases/download/v1.3.0/dribtard.exe";
    std::string notes = "Improved curve timing and stability";
    std::string nonce = std::string(32, 'e');
    long long now = 1700000000;

    std::string binaryPayload = "MZ-FAKE-DRIBTARD-PE-BINARY-PAYLOAD-1.3.0";
    std::string sha256;
    std::uint64_t size = 0;
    std::string releaseSig;

    UpdateFixture()
    {
        size = binaryPayload.size();
        Check(
            Update::ComputeSha256Hex(
                reinterpret_cast<const std::uint8_t*>(binaryPayload.data()),
                binaryPayload.size(),
                sha256),
            "ComputeSha256Hex should succeed");

        const std::string releaseMsg = Update::BuildReleaseSignedMessage(
            newVersion, sha256, size);
        releaseSig = SignHex(releaseKeys, releaseMsg);
    }

    std::string BuildManifestJson(
        const std::string& ver,
        const std::string& downloadUrl,
        const std::string& hashHex,
        std::uint64_t byteSize,
        const std::string& relNotes,
        const std::string& relSigHex) const
    {
        return "{\"version\":\"" + Licensing::JsonEscape(ver) +
            "\",\"url\":\"" + Licensing::JsonEscape(downloadUrl) +
            "\",\"sha256\":\"" + Licensing::JsonEscape(hashHex) +
            "\",\"size\":" + std::to_string(byteSize) +
            ",\"notes\":\"" + Licensing::JsonEscape(relNotes) +
            "\",\"releaseSig\":\"" + Licensing::JsonEscape(relSigHex) + "\"}";
    }

    std::string BuildUpdateResponseJson(
        const std::string& ver,
        const std::string& downloadUrl,
        const std::string& hashHex,
        std::uint64_t byteSize,
        const std::string& relNotes,
        const std::string& relSigHex,
        const std::string& respNonce,
        long long ts,
        const std::string& serverSigHex) const
    {
        return "{\"status\":\"ok\",\"manifest\":" +
            BuildManifestJson(ver, downloadUrl, hashHex, byteSize, relNotes, relSigHex) +
            ",\"nonce\":\"" + respNonce +
            "\",\"ts\":" + std::to_string(ts) +
            ",\"sig\":\"" + serverSigHex + "\"}";
    }
};

// Fake update transport that never uses the network:
// - Extracts the request nonce from POST /update JSON
// - Returns a signed response built with the configured callback
// - Writes the configured binary payload to destPath during DownloadFile
class FakeUpdateTransport : public Update::IUpdateTransport
{
public:
    using ResponseBuilder = std::function<std::string(const std::string& requestNonce)>;

    FakeUpdateTransport(
        ResponseBuilder responseBuilder,
        std::string downloadPayload)
        : m_ResponseBuilder(std::move(responseBuilder))
        , m_DownloadPayload(std::move(downloadPayload))
    {
    }

    Licensing::HttpResult PostUpdate(
        const std::string& serverUrl,
        const std::string& body) override
    {
        Check(serverUrl.rfind("https://", 0) == 0, "serverUrl must be https://");
        Check(!body.empty(), "POST /update body must not be empty");

        const std::string nonce = ExtractField(body, "nonce");
        const std::string currentVersion = ExtractField(body, "currentVersion");
        Check(!nonce.empty(), "request must include nonce");
        Check(!currentVersion.empty(), "request must include currentVersion");

        Licensing::HttpResult result;
        result.ok = true;
        result.body = m_ResponseBuilder(nonce);
        return result;
    }

    bool DownloadFile(
        const std::string& url,
        const std::string& destPath,
        std::uint64_t maxBytes,
        Update::ProgressCallback onProgress,
        std::string& outError) override
    {
        if (url.rfind("https://", 0) != 0)
        {
            outError = "URL must start with https://";
            return false;
        }

        if (m_DownloadPayload.size() > maxBytes)
        {
            outError = "Exceeds maxBytes";
            return false;
        }

        if (!WriteRawFile(destPath, m_DownloadPayload))
        {
            outError = "Could not write fake download file";
            return false;
        }

        if (onProgress)
        {
            onProgress(m_DownloadPayload.size() / 2, m_DownloadPayload.size());
            onProgress(m_DownloadPayload.size(), m_DownloadPayload.size());
        }

        return true;
    }

private:
    static std::string ExtractField(
        const std::string& body,
        const std::string& name)
    {
        const std::string token = "\"" + name + "\":\"";
        const std::size_t pos = body.find(token);

        if (pos == std::string::npos)
            return "";

        const std::size_t start = pos + token.size();
        const std::size_t end = body.find('"', start);

        if (end == std::string::npos)
            return "";

        return body.substr(start, end - start);
    }

    ResponseBuilder m_ResponseBuilder;
    std::string m_DownloadPayload;
};

void TestVersionComparison()
{
    Update::SemVer v{};
    Check(Update::ParseSemVer("1.0.0", v) && v.major == 1 && v.minor == 0 && v.patch == 0, "parse 1.0.0");
    Check(Update::ParseSemVer("v2.14.9", v) && v.major == 2 && v.minor == 14 && v.patch == 9, "parse v2.14.9");
    Check(!Update::ParseSemVer("", v), "reject empty version");
    Check(!Update::ParseSemVer("1.0", v), "reject 2-part version");
    Check(!Update::ParseSemVer("1.0.0.1", v), "reject 4-part version");
    Check(!Update::ParseSemVer("1.0.x", v), "reject non-numeric patch");

    Check(Update::CompareSemVer("1.3.0", "1.0.0") > 0, "1.3.0 > 1.0.0");
    Check(Update::CompareSemVer("2.0.0", "1.99.99") > 0, "2.0.0 > 1.99.99");
    Check(Update::CompareSemVer("1.0.1", "1.0.0") > 0, "1.0.1 > 1.0.0");
    Check(Update::CompareSemVer("1.0.0", "1.0.0") == 0, "1.0.0 == 1.0.0");
    Check(Update::CompareSemVer("0.9.9", "1.0.0") < 0, "0.9.9 < 1.0.0");
    Check(Update::CompareSemVer("1.2.0", "1.3.0") < 0, "1.2.0 < 1.3.0");

    Check(Update::IsNewerVersion("1.3.0", "1.0.0"), "IsNewerVersion true for upgrade");
    Check(!Update::IsNewerVersion("1.0.0", "1.0.0"), "IsNewerVersion false for same version");
    Check(!Update::IsNewerVersion("0.9.0", "1.0.0"), "IsNewerVersion false for downgrade");
}

void TestAcceptsGoodManifestAndPackage(const std::string& testDir)
{
    UpdateFixture f;

    auto transport = std::make_unique<FakeUpdateTransport>(
        [&](const std::string& reqNonce)
        {
            const long long ts = Licensing::NowUnixSeconds();
            const std::string msg = Update::BuildUpdateSignedMessage(
                f.newVersion, f.url, f.sha256, reqNonce, ts);
            const std::string sig = SignHex(f.serverKeys, msg);
            return f.BuildUpdateResponseJson(
                f.newVersion, f.url, f.sha256, f.size, f.notes, f.releaseSig,
                reqNonce, ts, sig);
        },
        f.binaryPayload);

    Update::Updater updater(
        "https://example.workers.dev",
        f.serverKeys.pub,
        f.releaseKeys.pub,
        f.currentVersion,
        std::move(transport));

    const Update::UpdateState checkState = updater.CheckNow("ABCD-EFGH-IJKL-MNOP", std::string(64, 'a'));
    Check(checkState.status == Update::UpdateStatus::UpdateAvailable, "good manifest should be accepted");
    Check(checkState.manifest.version == "1.3.0", "manifest version parsed");
    Check(checkState.manifest.notes == f.notes, "manifest notes parsed");

    const Update::UpdateState dlState = updater.DownloadAndVerifyNow(testDir);
    Check(dlState.status == Update::UpdateStatus::ReadyToApply, "good binary should verify and become ReadyToApply");
    Check(dlState.progress == 1.0f, "progress reaches 1.0");
}

void TestRejectsBadSignatures(const std::string& testDir)
{
    UpdateFixture f;

    // 1. Tampered server signature on /update response.
    {
        auto transport = std::make_unique<FakeUpdateTransport>(
            [&](const std::string& reqNonce)
            {
                const long long ts = Licensing::NowUnixSeconds();
                const std::string msg = Update::BuildUpdateSignedMessage(
                    f.newVersion, f.url, f.sha256, reqNonce, ts);
                std::string sig = SignHex(f.serverKeys, msg);
                sig.back() = (sig.back() == '0') ? '1' : '0';
                return f.BuildUpdateResponseJson(
                    f.newVersion, f.url, f.sha256, f.size, f.notes, f.releaseSig,
                    reqNonce, ts, sig);
            },
            f.binaryPayload);

        Update::Updater updater(
            "https://example.workers.dev",
            f.serverKeys.pub,
            f.releaseKeys.pub,
            f.currentVersion,
            std::move(transport));

        const Update::UpdateState state = updater.CheckNow("ABCD-EFGH-IJKL-MNOP", std::string(64, 'a'));
        Check(state.status == Update::UpdateStatus::Failed, "tampered server signature must fail");
        Check(state.errorReason == "bad_signature", "errorReason should be bad_signature");
    }

    // 2. Valid server signature, but releaseSig was signed by the WRONG key (e.g. server key instead of offline release key).
    {
        const std::string releaseMsg = Update::BuildReleaseSignedMessage(f.newVersion, f.sha256, f.size);
        const std::string wrongKeyReleaseSig = SignHex(f.serverKeys, releaseMsg);

        auto transport = std::make_unique<FakeUpdateTransport>(
            [&](const std::string& reqNonce)
            {
                const long long ts = Licensing::NowUnixSeconds();
                const std::string msg = Update::BuildUpdateSignedMessage(
                    f.newVersion, f.url, f.sha256, reqNonce, ts);
                const std::string sig = SignHex(f.serverKeys, msg);
                return f.BuildUpdateResponseJson(
                    f.newVersion, f.url, f.sha256, f.size, f.notes, wrongKeyReleaseSig,
                    reqNonce, ts, sig);
            },
            f.binaryPayload);

        Update::Updater updater(
            "https://example.workers.dev",
            f.serverKeys.pub,
            f.releaseKeys.pub,
            f.currentVersion,
            std::move(transport));

        const Update::UpdateState checkState = updater.CheckNow("ABCD-EFGH-IJKL-MNOP", std::string(64, 'a'));
        Check(checkState.status == Update::UpdateStatus::UpdateAvailable, "server response sig is valid");

        const Update::UpdateState dlState = updater.DownloadAndVerifyNow(testDir);
        Check(dlState.status == Update::UpdateStatus::Failed, "wrong release key signature must be rejected");
        Check(dlState.errorReason == "bad_release_signature", "errorReason should be bad_release_signature");
    }
}

void TestRejectsHashMismatch(const std::string& testDir)
{
    UpdateFixture f;

    // Tamper with the downloaded bytes so SHA-256 does not match the signed manifest.
    std::string corruptedBinary = f.binaryPayload;
    corruptedBinary[0] = 'X';

    auto transport = std::make_unique<FakeUpdateTransport>(
        [&](const std::string& reqNonce)
        {
            const long long ts = Licensing::NowUnixSeconds();
            const std::string msg = Update::BuildUpdateSignedMessage(
                f.newVersion, f.url, f.sha256, reqNonce, ts);
            const std::string sig = SignHex(f.serverKeys, msg);
            return f.BuildUpdateResponseJson(
                f.newVersion, f.url, f.sha256, f.size, f.notes, f.releaseSig,
                reqNonce, ts, sig);
        },
        corruptedBinary);

    Update::Updater updater(
        "https://example.workers.dev",
        f.serverKeys.pub,
        f.releaseKeys.pub,
        f.currentVersion,
        std::move(transport));

    const Update::UpdateState checkState = updater.CheckNow("ABCD-EFGH-IJKL-MNOP", std::string(64, 'a'));
    Check(checkState.status == Update::UpdateStatus::UpdateAvailable, "manifest check succeeds before download");

    const Update::UpdateState dlState = updater.DownloadAndVerifyNow(testDir);
    Check(dlState.status == Update::UpdateStatus::Failed, "hash mismatch must be rejected");
    Check(dlState.errorReason == "hash_mismatch", "errorReason should be hash_mismatch");
    Check(
        GetFileAttributesA((testDir + "\\dribtard.new.exe").c_str()) == INVALID_FILE_ATTRIBUTES,
        "corrupted dribtard.new.exe must be deleted on verification failure");
}

void TestRejectsDowngradeAndSameVersion()
{
    UpdateFixture f;

    // Server returns a validly signed manifest for v0.9.0 while client is on v1.0.0.
    const std::string downgradeVersion = "0.9.0";
    const std::string releaseMsg = Update::BuildReleaseSignedMessage(downgradeVersion, f.sha256, f.size);
    const std::string downgradeReleaseSig = SignHex(f.releaseKeys, releaseMsg);

    auto transport = std::make_unique<FakeUpdateTransport>(
        [&](const std::string& reqNonce)
        {
            const long long ts = Licensing::NowUnixSeconds();
            const std::string msg = Update::BuildUpdateSignedMessage(
                downgradeVersion, f.url, f.sha256, reqNonce, ts);
            const std::string sig = SignHex(f.serverKeys, msg);
            return f.BuildUpdateResponseJson(
                downgradeVersion, f.url, f.sha256, f.size, f.notes, downgradeReleaseSig,
                reqNonce, ts, sig);
        },
        f.binaryPayload);

    Update::Updater updater(
        "https://example.workers.dev",
        f.serverKeys.pub,
        f.releaseKeys.pub,
        "1.0.0",
        std::move(transport));

    const Update::UpdateState state = updater.CheckNow("ABCD-EFGH-IJKL-MNOP", std::string(64, 'a'));
    Check(state.status == Update::UpdateStatus::Failed, "downgrade manifest must be rejected");
    Check(state.errorReason == "downgrade_rejected", "errorReason should be downgrade_rejected");

    // Also test same version ("1.0.0" when running "1.0.0") is rejected if returned as status="ok".
    Update::UpdateCheckResponse sameResp;
    sameResp.status = "ok";
    sameResp.manifest.version = "1.0.0";
    sameResp.manifest.url = f.url;
    sameResp.manifest.sha256 = f.sha256;
    sameResp.manifest.size = f.size;
    sameResp.manifest.releaseSig = f.releaseSig;
    sameResp.nonce = f.nonce;
    sameResp.ts = f.now;
    sameResp.sigHex = SignHex(
        f.serverKeys,
        Update::BuildUpdateSignedMessage("1.0.0", f.url, f.sha256, f.nonce, f.now));

    bool hasUpdate = false;
    std::string reason;
    Check(
        !Update::VerifyUpdateCheckResponse(
            sameResp, f.nonce, "1.0.0", f.serverKeys.pub, f.now, hasUpdate, reason),
        "same version in 'ok' manifest must be rejected");
    Check(reason == "downgrade_rejected", "same version reason is downgrade_rejected");
}

void TestRejectsInsecureUrlAndStaleNonce()
{
    UpdateFixture f;

    // HTTP (non-HTTPS) URL in manifest must be rejected even if signed.
    const std::string httpUrl = "http://github.com/example/releases/download/v1.3.0/dribtard.exe";
    Update::UpdateCheckResponse resp;
    resp.status = "ok";
    resp.manifest.version = f.newVersion;
    resp.manifest.url = httpUrl;
    resp.manifest.sha256 = f.sha256;
    resp.manifest.size = f.size;
    resp.manifest.releaseSig = f.releaseSig;
    resp.nonce = f.nonce;
    resp.ts = f.now;
    resp.sigHex = SignHex(
        f.serverKeys,
        Update::BuildUpdateSignedMessage(f.newVersion, httpUrl, f.sha256, f.nonce, f.now));

    bool hasUpdate = false;
    std::string reason;
    Check(
        !Update::VerifyUpdateCheckResponse(
            resp, f.nonce, f.currentVersion, f.serverKeys.pub, f.now, hasUpdate, reason),
        "http:// URL must be rejected");
    Check(reason == "insecure_url", "insecure_url reason");

    // Signed "current" response verifies cleanly.
    Update::UpdateCheckResponse currentResp;
    currentResp.status = "current";
    currentResp.nonce = f.nonce;
    currentResp.ts = f.now;
    currentResp.sigHex = SignHex(
        f.serverKeys,
        Update::BuildCurrentSignedMessage(f.nonce, f.now));

    Check(
        Update::VerifyUpdateCheckResponse(
            currentResp, f.nonce, f.currentVersion, f.serverKeys.pub, f.now, hasUpdate, reason),
        "signed current response accepted");
    Check(!hasUpdate, "hasUpdate is false for current");
}

void TestApplySwapAndCleanup(const std::string& testDir)
{
    const std::string curPath = testDir + "\\dribtard.exe";
    const std::string newPath = testDir + "\\dribtard.new.exe";
    const std::string oldPath = testDir + "\\dribtard.old.exe";

    Check(WriteRawFile(curPath, "OLD-BINARY"), "write simulated running exe");
    Check(WriteRawFile(newPath, "NEW-BINARY"), "write simulated staged exe");

    std::string err;
    Check(
        Update::Updater::ApplyStagedUpdateForPaths(
            std::wstring(curPath.begin(), curPath.end()),
            std::wstring(newPath.begin(), newPath.end()),
            std::wstring(oldPath.begin(), oldPath.end()),
            false,
            err),
        "ApplyStagedUpdateForPaths should swap files");

    Check(ReadRawFile(curPath) == "NEW-BINARY", "dribtard.exe replaced with new binary");
    Check(ReadRawFile(oldPath) == "OLD-BINARY", "previous exe renamed to dribtard.old.exe");

    // Simulate a leftover dribtard.new.exe as well, then run startup cleanup.
    Check(WriteRawFile(newPath, "LEFTOVER-NEW"), "write leftover new exe");
    Update::Updater::CleanupOldFilesInDir(testDir);

    Check(
        GetFileAttributesA(oldPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "CleanupOldFiles deletes dribtard.old.exe");
    Check(
        GetFileAttributesA(newPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "CleanupOldFiles deletes dribtard.new.exe");
    Check(
        ReadRawFile(curPath) == "NEW-BINARY",
        "CleanupOldFiles keeps dribtard.exe intact");

    DeleteFileA(curPath.c_str());
}

} // namespace

int main()
{
    char tempDir[MAX_PATH]{};

    if (GetTempPathA(static_cast<DWORD>(sizeof(tempDir)), tempDir) == 0)
    {
        std::cerr << "Update test failed: no temp path" << std::endl;
        return 1;
    }

    const std::string testDir = std::string(tempDir) + "dribtard_update_tests";
    CreateDirectoryA(testDir.c_str(), nullptr);

    try
    {
        TestVersionComparison();
        TestAcceptsGoodManifestAndPackage(testDir);
        TestRejectsBadSignatures(testDir);
        TestRejectsHashMismatch(testDir);
        TestRejectsDowngradeAndSameVersion();
        TestRejectsInsecureUrlAndStaleNonce();
        TestApplySwapAndCleanup(testDir);

        std::cout << "All update tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Update test failed: " << e.what() << std::endl;
        return 1;
    }
}
