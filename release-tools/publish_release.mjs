// Publishes a signed dribtard release to GitHub Releases and updates the
// Cloudflare Worker KV manifest ("release:latest").
//
// Usage:
//   node release-tools/publish_release.mjs <version> <path-to-exe> "<notes>" [--dry-run]
//
// Example:
//   node release-tools/publish_release.mjs 1.3.0 "External Macro/build/Release/dribtard.exe" "Faster curve preset switching"

import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const configPath = path.join(__dirname, "config.json");
const privateKeyPath = path.join(__dirname, "release_private.pem");

// Resolve a command to its absolute path on Windows so it can be spawned
// WITHOUT a shell. Passing arguments through cmd.exe re-splits anything that
// contains spaces or punctuation (release notes, long JSON manifests), which
// silently breaks the upload.
function resolveExecutable(name)
{
    if (process.platform !== "win32")
        return name;

    const res = spawnSync("where", [name], { encoding: "utf8" });

    if (res.status === 0 && res.stdout)
    {
        const first = res.stdout.split(/\r?\n/).map((line) => line.trim()).filter(Boolean)[0];

        if (first)
            return first;
    }

    return name;
}

const ghExe = resolveExecutable("gh.exe");

// Locate wrangler's JavaScript entry point so it can be spawned with node.exe
// directly. Spawning the npx/wrangler .cmd shims requires a shell on Windows,
// and a shell re-splits every argument it is given.
function resolveWranglerJs(dir)
{
    const candidates = [
        path.join(dir, "node_modules", "wrangler", "bin", "wrangler.js"),
    ];

    // npm's global prefix locations, checked directly so no shell is needed.
    const globalPrefixes = [];

    if (process.env.npm_config_prefix)
        globalPrefixes.push(process.env.npm_config_prefix);

    if (process.platform === "win32")
    {
        if (process.env.APPDATA)
            globalPrefixes.push(path.join(process.env.APPDATA, "npm"));
    }
    else
    {
        globalPrefixes.push("/usr/local", "/usr");
    }

    for (const prefix of globalPrefixes)
    {
        candidates.push(path.join(
            prefix, "node_modules", "wrangler", "bin", "wrangler.js"));

        if (process.platform !== "win32")
        {
            candidates.push(path.join(
                prefix, "lib", "node_modules", "wrangler", "bin", "wrangler.js"));
        }
    }

    for (const candidate of candidates)
    {
        if (fs.existsSync(candidate))
            return candidate;
    }

    return null;
}

function printUsageAndExit(message)
{
    if (message)
        console.error("Error: " + message + "\n");

    console.error("Usage:");
    console.error("  node release-tools/publish_release.mjs <version> <path-to-exe> \"<notes>\" [--dry-run]");
    process.exit(1);
}

function parseSemver(version)
{
    const cleaned = String(version || "").trim().replace(/^[vV]/, "");
    if (!/^\d+\.\d+\.\d+$/.test(cleaned))
        return null;
    return cleaned;
}

function checkGhCli()
{
    const versionRes = spawnSync(ghExe, ["--version"], { encoding: "utf8" });
    if (versionRes.error || versionRes.status !== 0)
    {
        console.error("GitHub CLI (`gh`) is not installed or not on PATH.");
        console.error("Install it for free:");
        console.error("  winget install --id GitHub.cli");
        console.error("  (or download from https://cli.github.com/)");
        console.error("Then authenticate with:");
        console.error("  gh auth login");
        process.exit(1);
    }

    const authRes = spawnSync(ghExe, ["auth", "status"], { encoding: "utf8" });
    if (authRes.error || authRes.status !== 0)
    {
        console.error("GitHub CLI (`gh`) is not logged in.");
        console.error("Authenticate with your GitHub account:");
        console.error("  gh auth login");
        process.exit(1);
    }
}

const rawArgs = process.argv.slice(2);
const dryRun = rawArgs.includes("--dry-run");
const positional = rawArgs.filter((a) => a !== "--dry-run");

if (positional.length < 3)
    printUsageAndExit("Expected <version>, <path-to-exe>, and \"<notes>\".");

const version = parseSemver(positional[0]);
if (!version)
    printUsageAndExit("Version must be semantic X.Y.Z (e.g. 1.3.0).");

const exeInputPath = path.resolve(positional[1]);
const notes = String(positional[2]);

if (!fs.existsSync(exeInputPath))
    printUsageAndExit("Executable not found at: " + exeInputPath);

if (!fs.existsSync(configPath))
    printUsageAndExit("Missing config file: " + configPath);

let config;
try
{
    config = JSON.parse(fs.readFileSync(configPath, "utf8"));
}
catch (err)
{
    printUsageAndExit("Could not parse " + configPath + ": " + err.message);
}

const repo = String(config.releasesRepo || config.repo || "").trim();
if (!repo || repo.includes("YOUR_GITHUB_USER") || !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repo))
{
    printUsageAndExit(
        "Please set \"releasesRepo\" in release-tools/config.json to your GitHub \"<owner>/<releases-repo>\"."
    );
}

const assetName = String(config.assetName || "dribtard.exe").trim();
const wranglerDir = path.resolve(__dirname, config.wranglerDir || "../license-server");

if (!fs.existsSync(privateKeyPath))
{
    printUsageAndExit(
        "Release private key not found at " + privateKeyPath +
        ". Run `node release-tools/gen_release_key.mjs` first."
    );
}

const privateKeyPem = fs.readFileSync(privateKeyPath, "utf8");
const privateKey = crypto.createPrivateKey(privateKeyPem);

const exeBuffer = fs.readFileSync(exeInputPath);
const size = exeBuffer.length;
const sha256 = crypto.createHash("sha256").update(exeBuffer).digest("hex");

// Sign "release|" + version + "|" + sha256 + "|" + size with the offline release Ed25519 key.
const signedPayload = "release|" + version + "|" + sha256 + "|" + String(size);
const signatureBytes = crypto.sign(null, Buffer.from(signedPayload, "utf8"), privateKey);
const releaseSig = signatureBytes.toString("hex");

const tag = "v" + version;
const downloadUrl = "https://github.com/" + repo + "/releases/download/" + tag + "/" + assetName;

const manifest = {
    version,
    url: downloadUrl,
    sha256,
    size,
    notes,
    releaseSig,
};

const manifestJson = JSON.stringify(manifest);

console.log("Release summary:");
console.log("  Version:     " + version + " (tag " + tag + ")");
console.log("  Executable:  " + exeInputPath);
console.log("  Size:        " + String(size) + " bytes");
console.log("  SHA-256:     " + sha256);
console.log("  Signed msg:  " + signedPayload);
console.log("  releaseSig:  " + releaseSig);
console.log("  Download:    " + downloadUrl);
console.log();
console.log("Manifest JSON (KV key release:latest):");
console.log(JSON.stringify(manifest, null, 2));
console.log();

if (dryRun)
{
    console.log("[--dry-run] Skipping GitHub release upload and Cloudflare KV write.");
    console.log("Commands that would run:");
    console.log("  1. gh release create " + tag + " \"" + exeInputPath + "#" + assetName + "\" --repo " + repo + " --title \"v" + version + "\" --notes <notes>");
    console.log("  2. npx wrangler kv key put release:latest <manifest> --binding LICENSES --remote   (in " + wranglerDir + ")");
    process.exit(0);
}

checkGhCli();

// Ensure the uploaded asset is named dribtard.exe on the GitHub release even if
// the local file has a different path.
const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "dribtard-release-"));
const stagedExePath = path.join(tempDir, assetName);
fs.copyFileSync(exeInputPath, stagedExePath);

try
{
    console.log("Uploading " + assetName + " to GitHub release " + tag + " in " + repo + "...");

    const viewRes = spawnSync(
        ghExe,
        ["release", "view", tag, "--repo", repo],
        { encoding: "utf8" });

    if (viewRes.status === 0)
    {
        const uploadRes = spawnSync(
            ghExe,
            ["release", "upload", tag, stagedExePath, "--clobber", "--repo", repo],
            { stdio: "inherit" });

        if (uploadRes.status !== 0)
            process.exit(uploadRes.status || 1);
    }
    else
    {
        const createRes = spawnSync(
            ghExe,
            [
                "release", "create", tag,
                stagedExePath,
                "--repo", repo,
                "--title", "v" + version,
                "--notes", notes,
            ],
            { stdio: "inherit" });

        if (createRes.status !== 0)
            process.exit(createRes.status || 1);
    }
}
finally
{
    try
    {
        fs.rmSync(tempDir, { recursive: true, force: true });
    }
    catch (_)
    {
    }
}

console.log("Writing release:latest manifest to Cloudflare KV (binding LICENSES, --remote)...");

// The manifest travels in a file (--path) rather than on the command line so
// JSON quoting can never corrupt it.
const manifestFilePath = path.join(
    fs.mkdtempSync(path.join(os.tmpdir(), "dribtard-manifest-")),
    "release-latest.json");
fs.writeFileSync(manifestFilePath, manifestJson, "utf8");

const wranglerJs = resolveWranglerJs(wranglerDir);

if (!wranglerJs)
{
    console.error("Could not find wrangler. Run this yourself, from " + wranglerDir + ":");
    console.error("  npx wrangler kv key put release:latest --path \"" + manifestFilePath + "\" --binding LICENSES --remote");
    process.exit(1);
}

const kvRes = spawnSync(
    process.execPath,
    [
        wranglerJs,
        "kv", "key", "put",
        "release:latest",
        "--path", manifestFilePath,
        "--binding", "LICENSES",
        "--remote",
    ],
    {
        cwd: wranglerDir,
        stdio: "inherit",
    });

if (kvRes.status !== 0)
{
    console.error("Failed to write release:latest to Cloudflare KV.");
    console.error("The GitHub release was created; retry the KV write with, from " + wranglerDir + ":");
    console.error("  npx wrangler kv key put release:latest --path \"" + manifestFilePath + "\" --binding LICENSES --remote");
    process.exit(kvRes.status || 1);
}

try
{
    fs.rmSync(path.dirname(manifestFilePath), { recursive: true, force: true });
}
catch (_)
{
}

console.log("Release v" + version + " published and live in KV.");
