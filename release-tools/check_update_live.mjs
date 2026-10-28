// Preflight check for the auto-update pipeline.
//
// Verifies everything the client needs BEFORE you launch the app, so a failure
// in the GUI can be attributed to the client rather than to a stale or bad
// server-side release.
//
// Checks:
//   1. release:latest exists in Cloudflare KV and parses as a manifest
//   2. the manifest version is newer than a given current version
//   3. the download URL is https:// and the asset is publicly reachable
//   4. the downloaded size and SHA-256 match the manifest exactly
//   5. releaseSig verifies against the release public key read straight out of
//      LicenseConfig.hpp (the same constant the app compiles in)
//
// Usage:
//   node release-tools/check_update_live.mjs [currentVersion]
//
// Default currentVersion is the APP_VERSION in External Macro/src/Version.hpp.

import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "..");
const licenseConfigPath = path.join(repoRoot, "External Macro", "src", "Licensing", "LicenseConfig.hpp");
const versionHeaderPath = path.join(repoRoot, "External Macro", "src", "Version.hpp");
const wranglerDir = path.join(repoRoot, "license-server");

let failures = 0;

function report(ok, label, detail)
{
    console.log((ok ? "  PASS  " : "  FAIL  ") + label + (detail ? "  ->  " + detail : ""));

    if (!ok)
        failures++;
}

function readAppVersion()
{
    try
    {
        const text = fs.readFileSync(versionHeaderPath, "utf8");
        const m = text.match(/#define\s+APP_VERSION\s+"([^"]+)"/);
        return m ? m[1] : null;
    }
    catch (_)
    {
        return null;
    }
}

// Reads kReleasePublicKey out of LicenseConfig.hpp so this check uses the exact
// same bytes the application compiles in (no copy to drift out of sync).
function readEmbeddedReleasePublicKey()
{
    const text = fs.readFileSync(licenseConfigPath, "utf8");
    const declIndex = text.indexOf("kReleasePublicKey");

    if (declIndex < 0)
        return null;

    const braceStart = text.indexOf("{{", declIndex);

    if (braceStart < 0)
        return null;

    const braceEnd = text.indexOf("}}", braceStart);

    if (braceEnd < 0)
        return null;

    const body = text.slice(braceStart + 2, braceEnd);
    const bytes = body
        .split(",")
        .map((t) => t.trim())
        .filter(Boolean)
        .map((t) => parseInt(t, 16));

    if (bytes.length !== 32 || bytes.some((b) => !Number.isFinite(b) || b < 0 || b > 255))
        return null;

    return Buffer.from(bytes);
}

function resolveWranglerJs()
{
    const candidates = [
        path.join(wranglerDir, "node_modules", "wrangler", "bin", "wrangler.js"),
    ];

    if (process.env.npm_config_prefix)
        candidates.push(path.join(process.env.npm_config_prefix, "node_modules", "wrangler", "bin", "wrangler.js"));

    if (process.platform === "win32" && process.env.APPDATA)
        candidates.push(path.join(process.env.APPDATA, "npm", "node_modules", "wrangler", "bin", "wrangler.js"));

    for (const c of candidates)
    {
        if (fs.existsSync(c))
            return c;
    }

    return null;
}

function readManifestFromKv()
{
    const wranglerJs = resolveWranglerJs();

    if (!wranglerJs)
        return { ok: false, error: "wrangler not found" };

    const res = spawnSync(
        process.execPath,
        [wranglerJs, "kv", "key", "get", "release:latest", "--binding", "LICENSES", "--remote"],
        { cwd: wranglerDir, encoding: "utf8" });

    if (res.status !== 0)
        return { ok: false, error: "wrangler exited " + res.status };

    const raw = (res.stdout || "").trim();

    if (!raw)
        return { ok: false, error: "release:latest is missing or empty" };

    try
    {
        return { ok: true, manifest: JSON.parse(raw) };
    }
    catch (err)
    {
        return { ok: false, error: "release:latest is not valid JSON: " + err.message };
    }
}

function compareSemver(a, b)
{
    const pa = String(a).replace(/^v/i, "").split(".").map((n) => parseInt(n, 10));
    const pb = String(b).replace(/^v/i, "").split(".").map((n) => parseInt(n, 10));

    for (let i = 0; i < 3; i++)
    {
        const x = pa[i] || 0;
        const y = pb[i] || 0;

        if (x !== y)
            return x > y ? 1 : -1;
    }

    return 0;
}

function publicKeyToSpki(raw)
{
    return crypto.createPublicKey({
        key: Buffer.concat([Buffer.from("302a300506032b6570032100", "hex"), raw]),
        format: "der",
        type: "spki",
    });
}

const requestedVersion = process.argv[2] || readAppVersion() || "0.0.0";

console.log("Auto-update preflight");
console.log("  installed version to simulate: " + requestedVersion);
console.log("");

// --- 1. release public key embedded in the source ---
const releasePub = readEmbeddedReleasePublicKey();

report(
    releasePub !== null,
    "kReleasePublicKey parses from LicenseConfig.hpp",
    releasePub ? releasePub.toString("hex").slice(0, 16) + "..." +
        (releasePub.every((b) => b === 0) ? "  (ALL ZEROS - updates will always be rejected!)" : "") : "");

if (releasePub === null || releasePub.every((b) => b === 0))
{
    console.log("");
    console.log(failures + " check(s) failed.");
    process.exit(1);
}

// --- 2. manifest present in KV ---
const kv = readManifestFromKv();

report(kv.ok, "release:latest readable from Cloudflare KV", kv.ok ? "" : kv.error);

if (!kv.ok)
{
    console.log("");
    console.log(failures + " check(s) failed.");
    process.exit(1);
}

const manifest = kv.manifest;

// --- 3. manifest shape ---
const shapeOk =
    typeof manifest.version === "string" &&
    typeof manifest.url === "string" &&
    typeof manifest.sha256 === "string" &&
    typeof manifest.size === "number" &&
    typeof manifest.releaseSig === "string";

report(shapeOk, "manifest has version/url/sha256/size/releaseSig", "v" + manifest.version);

report(manifest.url.startsWith("https://"), "download URL is https://", manifest.url);

// --- 4. would a client on <requestedVersion> be offered this? ---
const cmp = compareSemver(manifest.version, requestedVersion);

report(
    cmp > 0,
    "a client on v" + requestedVersion + " would be prompted",
    cmp > 0
        ? "v" + manifest.version + " is newer"
        : "v" + manifest.version + " is NOT newer than v" + requestedVersion +
          (cmp === 0 ? " (identical) - the app will show no banner, and that is correct" : " (downgrade - rejected)"));

// --- 5. asset is publicly reachable and byte-for-byte correct ---
const res = await fetch(manifest.url, { redirect: "follow" });

if (!res.ok)
{
    report(false, "release asset downloads", "HTTP " + res.status);
    console.log("");
    console.log(failures + " check(s) failed.");
    process.exit(1);
}

const buf = Buffer.from(await res.arrayBuffer());
const sha = crypto.createHash("sha256").update(buf).digest("hex");

report(buf.length === manifest.size, "downloaded size matches manifest", buf.length + " vs " + manifest.size);
report(sha === manifest.sha256, "downloaded SHA-256 matches manifest", sha);

// --- 6. release signature verifies against the embedded key ---
const message = "release|" + manifest.version + "|" + manifest.sha256 + "|" + manifest.size;
const sigOk = crypto.verify(
    null,
    Buffer.from(message),
    publicKeyToSpki(releasePub),
    Buffer.from(manifest.releaseSig, "hex"));

report(sigOk, "releaseSig verifies against kReleasePublicKey", message.slice(0, 48) + "...");

// --- 7. the Worker is deployed and answering ---
try
{
    const probe = await fetch("https://dribtard-license-server.kasonjonesrios.workers.dev/update",
        {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                key: "ZZZZ-ZZZZ-ZZZZ-ZZZZ",
                hwid: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                nonce: "deadbeef00112233445566778899aabb",
                currentVersion: requestedVersion,
            }),
        });

    const body = await probe.text();
    report(
        probe.status === 200 && body.includes("unknown_key"),
        "POST /update is live and validates licenses",
        probe.status + " " + body);
}
catch (err)
{
    report(false, "POST /update is live and validates licenses", err.message);
}

console.log("");

if (failures === 0)
{
    console.log("All checks passed. The app on v" + requestedVersion +
        " should show \"Version " + manifest.version + " available\" after its license validates.");
    process.exit(0);
}

console.log(failures + " check(s) failed.");
process.exit(1);
