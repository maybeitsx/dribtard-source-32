// Unit tests for the license response verification logic.
//
// Like MacroSequenceTests' fake input backend, these tests use a fake
// transport so no network is touched. Signatures are produced at runtime with
// Monocypher (test-only keypair), then verified through the production code.

#include "Licensing/LicenseClient.hpp"

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

TestKeys MakeTestKeys()
{
    // Fixed seed keeps the tests deterministic.
    std::array<std::uint8_t, 32> seed{};

    for (std::size_t i = 0; i < seed.size(); ++i)
        seed[i] = static_cast<std::uint8_t>(i + 1);

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

std::string MakeResponseJson(
    const std::string& status,
    const std::string& key,
    const std::string& hwid,
    const std::string& nonce,
    long long ts,
    long long expiresAt,
    const std::string& sigHex,
    const std::string& reason = "")
{
    std::string body = "{\"status\":\"" + status + "\"";

    if (!reason.empty())
        body += ",\"reason\":\"" + reason + "\"";

    body += ",\"key\":\"" + key + "\"";
    body += ",\"hwid\":\"" + hwid + "\"";
    body += ",\"nonce\":\"" + nonce + "\"";
    body += ",\"ts\":" + std::to_string(ts);
    body += ",\"expiresAt\":" + std::to_string(expiresAt);
    body += ",\"sig\":\"" + sigHex + "\"}";
    return body;
}

struct Fixture
{
    TestKeys keys = MakeTestKeys();
    std::string key = "ABCD-EFGH-IJKL-MNOP";
    std::string hwid = std::string(64, 'a');
    std::string nonce = std::string(32, 'b');
    long long now = 1700000000;
    long long expiresAt = 1800000000;

    std::string ValidJson()
    {
        const std::string message = Licensing::BuildSignedMessage(
            key, hwid, nonce, now, expiresAt);

        return MakeResponseJson(
            "valid", key, hwid, nonce, now, expiresAt,
            SignHex(keys, message));
    }

    Licensing::ValidateResponse ParseValid()
    {
        Licensing::ValidateResponse response;
        Check(
            Licensing::ParseValidateResponse(ValidJson(), response),
            "valid JSON should parse");

        return response;
    }

    bool Verify(const Licensing::ValidateResponse& response, std::string& reason, long long& expires)
    {
        return Licensing::VerifyLicenseResponse(
            response, key, hwid, nonce, keys.pub, now, reason, expires);
    }
};

// Fake transport: returns a canned body, never touches the network.
class FakeTransport : public Licensing::IValidateTransport
{
public:
    explicit FakeTransport(std::string body)
        : m_Body(std::move(body))
    {
    }

    Licensing::HttpResult PostValidate(
        const std::string& serverUrl,
        const std::string& body) override
    {
        (void)serverUrl;

        Check(!body.empty(), "request body should not be empty");
        Check(
            body.find("/validate") == std::string::npos,
            "request body is JSON, not a URL");

        Licensing::HttpResult result;
        result.ok = true;
        result.body = m_Body;
        return result;
    }

private:
    std::string m_Body;
};

class FailingTransport : public Licensing::IValidateTransport
{
public:
    Licensing::HttpResult PostValidate(
        const std::string&,
        const std::string&) override
    {
        Licensing::HttpResult result;
        result.ok = false;
        result.error = "no network in tests";
        return result;
    }
};

void TestWebCryptoInteropVector()
{
    // Signature produced by Node's WebCrypto Ed25519 (the same API the
    // Worker uses to sign). Verifying it here proves the C++ Monocypher
    // verifier interoperates with Worker signatures.
    std::array<std::uint8_t, 32> pub{};
    Check(
        Licensing::HexToBytes(
            "6da378a16f180e6483e9b7c980461ccecb59d05df047bccdabdc983f5b77391d",
            pub.data(), pub.size()),
        "interop pubkey");

    std::array<std::uint8_t, 64> sig{};
    Check(
        Licensing::HexToBytes(
            "1da62ad120e5e2f2384ca5e3ce09ca11a5c6cb2499c4675fc0a13e63badb1b3a1"
            "11d267b9875a576331a384b0f92e359617b74797ab8f0e302464de5a6489907",
            sig.data(), sig.size()),
        "interop sig");

    const std::string message =
        "valid|TEST-KEY|abcdef0123456789|0123456789abcdef|1700000000|0";

    Check(Licensing::VerifyEd25519(pub, message, sig), "interop vector verifies");

    sig[0] ^= 0x01;
    Check(!Licensing::VerifyEd25519(pub, message, sig), "interop tampered fails");
}

void TestSignedMessageFormat()
{
    Check(
        Licensing::BuildSignedMessage("K", "H", "N", 1, 2) == "valid|K|H|N|1|2",
        "signed message format");
}

void TestHexRoundTrip()
{
    const std::uint8_t bytes[] = { 0x12, 0x34, 0xAB, 0xCD, 0x00, 0xFF };
    const std::string hex = Licensing::BytesToHex(bytes, sizeof(bytes));
    Check(hex == "1234abcd00ff", "hex encoding");

    std::uint8_t back[6]{};
    Check(Licensing::HexToBytes(hex, back, sizeof(back)), "hex decoding");

    for (std::size_t i = 0; i < sizeof(bytes); ++i)
        Check(back[i] == bytes[i], "hex round trip");

    Check(!Licensing::HexToBytes("zz", back, 1), "bad hex rejected");
    Check(!Licensing::HexToBytes("123", back, 1), "odd hex rejected");
}

void TestNonce()
{
    const std::string a = Licensing::GenerateNonceHex();
    const std::string b = Licensing::GenerateNonceHex();
    Check(a.size() == 32, "nonce length");
    Check(a != b, "nonces differ");
}

void TestParse()
{
    Fixture f;
    const Licensing::ValidateResponse response = f.ParseValid();
    Check(response.status == "valid", "status parsed");
    Check(response.key == f.key, "key parsed");
    Check(response.hwid == f.hwid, "hwid parsed");
    Check(response.nonce == f.nonce, "nonce parsed");
    Check(response.ts == f.now, "ts parsed");
    Check(response.expiresAt == f.expiresAt, "expiresAt parsed");
    Check(!response.sigHex.empty(), "sig parsed");

    Licensing::ValidateResponse bad;
    Check(!Licensing::ParseValidateResponse("not json", bad), "garbage rejected");
    Check(!Licensing::ParseValidateResponse("{}", bad), "missing status rejected");
}

void TestAcceptsValid()
{
    Fixture f;
    const Licensing::ValidateResponse response = f.ParseValid();

    std::string reason;
    long long expires = 0;
    Check(f.Verify(response, reason, expires), "valid response accepted");
    Check(reason.empty(), "no reason on success");
    Check(expires == f.expiresAt, "expiry returned");
}

void TestRejectsWrongNonce()
{
    Fixture f;
    Licensing::ValidateResponse response = f.ParseValid();
    response.nonce = std::string(32, 'c');

    std::string reason;
    long long expires = 0;
    Check(!f.Verify(response, reason, expires), "wrong nonce rejected");
    Check(reason == "nonce_mismatch", "nonce reason");
}

void TestRejectsWrongHwid()
{
    Fixture f;
    Licensing::ValidateResponse response = f.ParseValid();
    response.hwid = std::string(64, 'd');

    std::string reason;
    long long expires = 0;
    Check(!f.Verify(response, reason, expires), "wrong hwid rejected");
    Check(reason == "hwid_mismatch", "hwid reason");
}

void TestRejectsStaleTimestamp()
{
    Fixture f;
    Licensing::ValidateResponse response = f.ParseValid();
    response.ts = f.now - 301; // just outside the 5-minute window

    std::string reason;
    long long expires = 0;
    // Re-sign with the stale ts so only the freshness check can fail.
    const std::string message = Licensing::BuildSignedMessage(
        f.key, f.hwid, f.nonce, response.ts, f.expiresAt);
    response.sigHex = SignHex(f.keys, message);

    Check(!f.Verify(response, reason, expires), "stale ts rejected");
    Check(reason == "stale_response", "stale reason");
}

void TestRejectsFutureTimestamp()
{
    Fixture f;
    Licensing::ValidateResponse response = f.ParseValid();
    response.ts = f.now + 301;

    const std::string message = Licensing::BuildSignedMessage(
        f.key, f.hwid, f.nonce, response.ts, f.expiresAt);
    response.sigHex = SignHex(f.keys, message);

    std::string reason;
    long long expires = 0;
    Check(!f.Verify(response, reason, expires), "future ts rejected");
    Check(reason == "stale_response", "future ts reason");
}

void TestRejectsTamperedSignature()
{
    Fixture f;
    Licensing::ValidateResponse response = f.ParseValid();

    // Flip the last hex digit; the message is unchanged so verification must fail.
    response.sigHex.back() = (response.sigHex.back() == '0') ? '1' : '0';

    std::string reason;
    long long expires = 0;
    Check(!f.Verify(response, reason, expires), "tampered sig rejected");
    Check(reason == "bad_signature", "sig reason");
}

void TestRejectsExpired()
{
    Fixture f;
    f.expiresAt = f.now - 10;
    const Licensing::ValidateResponse response = f.ParseValid();

    std::string reason;
    long long expires = 0;
    Check(!f.Verify(response, reason, expires), "expired rejected");
    Check(reason == "expired", "expired reason");
}

void TestRejectsServerInvalid()
{
    Fixture f;
    Licensing::ValidateResponse response;
    Check(
        Licensing::ParseValidateResponse(
            MakeResponseJson("invalid", f.key, f.hwid, f.nonce, f.now, 0, "", "revoked"),
            response),
        "invalid JSON parses");

    std::string reason;
    long long expires = 0;
    Check(!f.Verify(response, reason, expires), "server invalid rejected");
    Check(reason == "revoked", "server reason passes through");
}

void TestRejectsUnconfiguredKey()
{
    Fixture f;
    const Licensing::ValidateResponse response = f.ParseValid();

    const std::array<std::uint8_t, 32> zeros{};
    std::string reason;
    long long expires = 0;
    Check(
        !Licensing::VerifyLicenseResponse(
            response, f.key, f.hwid, f.nonce, zeros, f.now, reason, expires),
        "zero public key rejected");
    Check(reason == "not_configured", "not_configured reason");
}

// Fake license server: parses key/hwid/nonce out of the request, signs a
// valid response for them (like the real Worker), and returns it.
class SigningFakeTransport : public Licensing::IValidateTransport
{
public:
    SigningFakeTransport(const TestKeys* keys, long long expiresAt)
        : m_Keys(keys)
        , m_ExpiresAt(expiresAt)
    {
    }

    Licensing::HttpResult PostValidate(
        const std::string& serverUrl,
        const std::string& body) override
    {
        Check(
            serverUrl.rfind("https://", 0) == 0,
            "transport uses https");

        const std::string key = Extract(body, "key");
        const std::string hwid = Extract(body, "hwid");
        const std::string nonce = Extract(body, "nonce");
        Check(!key.empty() && !hwid.empty() && !nonce.empty(), "request fields");

        const long long now = Licensing::NowUnixSeconds();
        const std::string message = Licensing::BuildSignedMessage(
            key, hwid, nonce, now, m_ExpiresAt);

        Licensing::HttpResult result;
        result.ok = true;
        result.body = MakeResponseJson(
            "valid", key, hwid, nonce, now, m_ExpiresAt,
            SignHex(*m_Keys, message));
        return result;
    }

private:
    static std::string Extract(const std::string& body, const std::string& name)
    {
        const std::string token = "\"" + name + "\":\"";
        const std::size_t begin = body.find(token);

        if (begin == std::string::npos)
            return "";

        const std::size_t valueBegin = begin + token.size();
        const std::size_t end = body.find('"', valueBegin);

        if (end == std::string::npos)
            return "";

        return body.substr(valueBegin, end - valueBegin);
    }

    const TestKeys* m_Keys;
    long long m_ExpiresAt;
};

void TestManagerOnlineFlow()
{
    // End-to-end through LicenseManager with a fake transport (no network).
    Fixture f;

    auto transport = std::make_unique<SigningFakeTransport>(&f.keys, 0);

    Licensing::LicenseManager manager(
        "https://example.workers.dev",
        f.keys.pub,
        std::move(transport));

    const Licensing::LicenseState state = manager.ValidateNow("TEST-KEY-1234");
    Check(state.status == Licensing::LicenseStatus::Valid, "manager valid");
    Check(manager.IsValid(), "manager IsValid");
    Check(state.expiryText.find("lifetime") != std::string::npos, "lifetime expiry");

    // An "invalid" verdict from the server surfaces with its reason.
    auto invalidTransport = std::make_unique<FakeTransport>(
        MakeResponseJson("invalid", "TEST-KEY-1234", "h", "n", 0, 0, "", "revoked"));

    Licensing::LicenseManager invalidManager(
        "https://example.workers.dev",
        f.keys.pub,
        std::move(invalidTransport));

    const Licensing::LicenseState invalidState =
        invalidManager.ValidateNow("TEST-KEY-1234");
    Check(
        invalidState.status == Licensing::LicenseStatus::Invalid,
        "revoked invalid");
    Check(invalidState.reason == "revoked", "revoked reason");
    Check(!invalidManager.IsValid(), "manager not valid");
}

void TestManagerOfflineFallback()
{
    // A successful online check caches the signed response; with the network
    // down the manager must report Offline (verified cache), never silently
    // Valid and never Invalid for a fresh cache.
    Fixture f;

    {
        auto online = std::make_unique<SigningFakeTransport>(&f.keys, 0);

        Licensing::LicenseManager seeder(
            "https://example.workers.dev",
            f.keys.pub,
            std::move(online));

        const Licensing::LicenseState seeded = seeder.ValidateNow("TEST-KEY-1234");
        Check(seeded.status == Licensing::LicenseStatus::Valid, "seed cache");
    }

    auto failing = std::make_unique<FailingTransport>();

    Licensing::LicenseManager manager(
        "https://example.workers.dev",
        f.keys.pub,
        std::move(failing));

    const Licensing::LicenseState state = manager.ValidateNow("TEST-KEY-1234");
    Check(state.status == Licensing::LicenseStatus::Offline, "offline fallback");
    Check(manager.IsValid(), "offline counts as valid");
}

} // namespace

int main()
{
    // Isolate the license files: the manager tests write a key cache, which
    // must land in a temp dir and never in the real %APPDATA%\dribtard.
    char tempDir[MAX_PATH]{};

    if (GetTempPathA(static_cast<DWORD>(sizeof(tempDir)), tempDir) == 0)
    {
        std::cerr << "License test failed: no temp path" << std::endl;
        return 1;
    }

    const std::string testDir = std::string(tempDir) + "dribtard_license_tests";
    CreateDirectoryA(testDir.c_str(), nullptr);
    SetEnvironmentVariableA("DRIBTARD_LICENSE_DIR", testDir.c_str());

    try
    {
        TestWebCryptoInteropVector();
        TestSignedMessageFormat();
        TestHexRoundTrip();
        TestNonce();
        TestParse();
        TestAcceptsValid();
        TestRejectsWrongNonce();
        TestRejectsWrongHwid();
        TestRejectsStaleTimestamp();
        TestRejectsFutureTimestamp();
        TestRejectsTamperedSignature();
        TestRejectsExpired();
        TestRejectsServerInvalid();
        TestRejectsUnconfiguredKey();
        TestManagerOnlineFlow();
        TestManagerOfflineFallback();

        std::cout << "All license tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "License test failed: " << e.what() << std::endl;
        return 1;
    }
}
