# dribtard license server (Cloudflare Worker, free tier)

Validates license keys for the Windows app and manages keys from Discord
slash commands. No always-on bot process: Discord calls the Worker's
`/discord` interactions endpoint directly.

## Prerequisites (all free)

- A Cloudflare account (Workers + KV are on the free tier)
- Node.js 18+ (for `wrangler` and the helper scripts)
- A Discord application (https://discord.com/developers/applications)

## 1. Install and log in

```powershell
cd license-server
npm install
npx wrangler login
```

## 2. Create the KV namespace

```powershell
npx wrangler kv:namespace create LICENSES
npx wrangler kv:namespace create LICENSES --preview
```

Paste both IDs into `wrangler.toml` (`id` and `preview_id`).

## 3. Generate the signing keypair

```powershell
node generate_keys.mjs
```

This prints:

- `SIGNING_PRIVATE_KEY` (PKCS#8 base64) — set as a Worker secret (next step)
- the public key as hex and as a C++ array — paste the C++ array into
  `External Macro/src/Licensing/LicenseConfig.hpp` (`kServerPublicKey`)

## 4. Set secrets (never commit these)

```powershell
npx wrangler secret put SIGNING_PRIVATE_KEY   # paste the PKCS#8 base64
npx wrangler secret put DISCORD_PUBLIC_KEY    # Discord app's public key (64 hex chars)
npx wrangler secret put ADMIN_IDS             # e.g. "123456789,987654321"
```

- `DISCORD_PUBLIC_KEY`: Discord Developer Portal → your application →
  General Information → Public Key.
- `ADMIN_IDS`: your Discord user ID (Developer Mode → right-click your name →
  Copy User ID). Only these users can run the key commands.

## 5. Deploy

```powershell
npx wrangler deploy
```

Note the URL, e.g. `https://dribtard-license-server.<you>.workers.dev`.
Set it as `kServerUrl` in
`External Macro/src/Licensing/LicenseConfig.hpp` and rebuild the app.

## 6. Point Discord at the Worker

1. Discord Developer Portal → your application → General Information →
   **Interactions Endpoint URL**: `https://<your-worker>/discord`
2. Discord verifies the endpoint with a PING — the Worker answers it, so the
   URL saves immediately when the secrets are set.
3. Enable the bot's application commands scope if prompted (no bot process
   is needed; the token below is only for one-time command registration).

## 7. Register the slash commands (one time)

Create a bot token (Bot → Reset Token) and run — the token is only used
here, never in the Worker or the C++ app:

```powershell
$env:DISCORD_APP_ID = "<application-id>"
$env:DISCORD_BOT_TOKEN = "<bot-token>"
node register_commands.mjs
```

Global commands can take up to ~1 hour to appear in Discord.

## 8. Manage keys from Discord (admin only, all replies ephemeral)

- `/genkey [days] [count] [note]` — create keys (`days` omitted = lifetime)
- `/revoke key:<KEY>` — revoke a key
- `/resethwid key:<KEY>` — clear the hardware binding
- `/keyinfo key:<KEY>` — status, expiry, binding
- `/listkeys` — up to 25 keys with status

## How validation works

`POST /validate` with `{"key","hwid","nonce"}`:

- Unknown / revoked / expired keys → `{"status":"invalid","reason":"..."}`
- First use binds the key to the requesting HWID; a different HWID later →
  `invalid` with reason `hwid_mismatch`
- Success returns the echoed `nonce`, unix `ts`, `expiresAt` (0 = lifetime)
  and an Ed25519 `sig` over `valid|key|hwid|nonce|ts|expiresAt`
- Basic rate limiting (30/min per IP, 10/min per key, KV-backed) slows
  brute-forcing

## Local development

```powershell
npx wrangler dev
```

## Files

- `worker.js` — the Worker (`/validate` + `/update` + `/discord`, Ed25519 via WebCrypto)
- `wrangler.toml` — Worker config + `LICENSES` KV binding
- `generate_keys.mjs` — creates the Ed25519 keypair for the Worker (Node built-in crypto only)
- `register_commands.mjs` — registers slash commands (env vars only, no hardcoding)
- `package.json` — `wrangler` is the only dependency

---

## Releasing an update

The auto-update system uses **two independent Ed25519 keypairs** so that even
if the Cloudflare Worker were compromised, an attacker could not push a binary
update:

1. **Server signing key (`SIGNING_PRIVATE_KEY`)** — lives as a Cloudflare Worker
   secret and signs `/validate` and `/update` JSON responses (`kServerPublicKey`
   in `LicenseConfig.hpp`).
2. **Release signing key (`release-tools/release_private.pem`)** — lives **only
   on your developer machine** (gitignored, **never** uploaded to Cloudflare) and
   signs the release binary's version, SHA-256, and byte size
   (`kReleasePublicKey` in `LicenseConfig.hpp`).

### One-time setup

#### 1. Create a public GitHub releases repository and set up `gh`

Create a dedicated public repository on GitHub (e.g. `<your-user>/dribtard-releases`)
that holds only release binaries (`dribtard.exe`), keeping your source code private.

Install the free GitHub CLI (`gh`) and log in:

```powershell
winget install --id GitHub.cli
gh auth login
```

Create the releases repository on GitHub (either on https://github.com/new or via `gh`):

```powershell
gh repo create <your-user>/dribtard-releases --public --description "dribtard release binaries"
```

Edit `release-tools/config.json` and set `releasesRepo` to `<your-user>/<releases-repo>`:

```json
{
  "releasesRepo": "your-github-username/dribtard-releases",
  "assetName": "dribtard.exe",
  "wranglerDir": "../license-server"
}
```

#### 2. Generate the offline release signing keypair

From the workspace root, run:

```powershell
node release-tools/gen_release_key.mjs
```

This writes `release-tools/release_private.pem` (ignored by `.gitignore` — back it
up safely offline and **never** commit it or upload it to Cloudflare) and prints a
32-byte C++ array.

Paste that C++ array into `External Macro/src/Licensing/LicenseConfig.hpp` as
`kReleasePublicKey`.

#### 3. Deploy the updated Worker

```powershell
cd license-server
npx wrangler deploy
```

### Publishing a new release (build to publish)

1. **Bump `APP_VERSION`**: edit `External Macro/src/Version.hpp` and set the new
   semantic version (for example `"1.3.0"`).
2. **Build the Release executable**:

   ```powershell
   cmake --build "External Macro/build" --config Release
   ```

3. **(Optional) Dry-run the release script** to inspect the computed SHA-256,
   byte size, `releaseSig`, and KV manifest JSON without uploading anything:

   ```powershell
   node release-tools/publish_release.mjs 1.3.0 "External Macro/build/Release/dribtard.exe" "Faster curve presets and bug fixes" --dry-run
   ```

4. **Publish the release** (uploads `dribtard.exe` to GitHub Releases with `gh`
   and writes `release:latest` to Cloudflare KV with `wrangler kv key put release:latest ... --binding LICENSES --remote`):

   ```powershell
   node release-tools/publish_release.mjs 1.3.0 "External Macro/build/Release/dribtard.exe" "Faster curve presets and bug fixes"
   ```

Licensed clients will see `"Version 1.3.0 available"` in the footer after their
next update check, can click **Update now** to stop the macro, download
`dribtard.new.exe`, verify the size, SHA-256, and `releaseSig`, swap the
executable (`dribtard.old.exe`), and relaunch automatically.

