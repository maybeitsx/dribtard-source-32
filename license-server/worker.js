// Cloudflare Worker: license validation + Discord slash-command key management.
//
// Secrets (wrangler secret put):
//   DISCORD_PUBLIC_KEY   - Discord application public key, 64 hex chars (32 bytes)
//   SIGNING_PRIVATE_KEY  - Ed25519 private key, PKCS#8 DER base64 (from generate_keys.mjs)
//   ADMIN_IDS            - comma-separated Discord user IDs allowed to manage keys
// KV binding:
//   LICENSES            - KV namespace bound as LICENSES
//
// Endpoints:
//   POST /validate  {key, hwid, nonce} -> signed validation response
//   POST /update    {key, hwid, nonce, currentVersion} -> signed update manifest or current status
//   POST /discord   Discord interactions endpoint (Ed25519 verified)

const encoder = new TextEncoder();
const decoder = new TextDecoder();

// Cached WebCrypto keys across requests in the same isolate.
let discordVerifyKey = null;
let signingKey = null;

function hexToBytes(hex, expectedBytes)
{
    if (typeof hex !== "string" ||
        hex.length !== expectedBytes * 2 ||
        !/^[0-9a-fA-F]+$/.test(hex))
    {
        throw new Error("Invalid hex string");
    }

    const bytes = new Uint8Array(expectedBytes);

    for (let i = 0; i < expectedBytes; i++)
        bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);

    return bytes;
}

function bytesToHex(bytes)
{
    let out = "";

    for (const b of bytes)
        out += b.toString(16).padStart(2, "0");

    return out;
}

function base64ToBytes(b64)
{
    const bin = atob(b64.replace(/\s+/g, ""));
    const bytes = new Uint8Array(bin.length);

    for (let i = 0; i < bin.length; i++)
        bytes[i] = bin.charCodeAt(i);

    return bytes;
}

function jsonResponse(data, status = 200)
{
    return new Response(JSON.stringify(data),
        {
            status,
            headers: { "Content-Type": "application/json" },
        });
}

async function getDiscordVerifyKey(publicKeyHex)
{
    if (discordVerifyKey)
        return discordVerifyKey;

    const raw = hexToBytes(publicKeyHex.trim(), 32);

    discordVerifyKey = await crypto.subtle.importKey(
        "raw",
        raw,
        { name: "Ed25519" },
        false,
        ["verify"]);

    return discordVerifyKey;
}

async function getSigningKey(pkcs8Base64)
{
    if (signingKey)
        return signingKey;

    const der = base64ToBytes(pkcs8Base64.trim());

    signingKey = await crypto.subtle.importKey(
        "pkcs8",
        der,
        { name: "Ed25519" },
        false,
        ["sign"]);

    return signingKey;
}

async function verifyDiscordRequest(request, publicKeyHex)
{
    const signatureHex = request.headers.get("X-Signature-Ed25519");
    const timestamp = request.headers.get("X-Signature-Timestamp");

    if (!signatureHex || !timestamp)
        return { valid: false, rawBody: null };

    // Read the body once as raw bytes; the signature covers the exact bytes.
    const rawBody = new Uint8Array(await request.arrayBuffer());

    try
    {
        const signature = hexToBytes(signatureHex, 64);
        const key = await getDiscordVerifyKey(publicKeyHex);
        const timestampBytes = encoder.encode(timestamp);
        const message = new Uint8Array(timestampBytes.length + rawBody.length);

        message.set(timestampBytes, 0);
        message.set(rawBody, timestampBytes.length);

        const valid = await crypto.subtle.verify(
            { name: "Ed25519" },
            key,
            signature,
            message);

        return { valid, rawBody };
    }
    catch (err)
    {
        return { valid: false, rawBody };
    }
}

// ---------------------------------------------------------------------------
// License keys
// ---------------------------------------------------------------------------

const KEY_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // 32 chars, no 0/O/1/I

function randomKey()
{
    // 16 chars x 5 bits = 80 bits of entropy, formatted XXXX-XXXX-XXXX-XXXX.
    const bytes = new Uint8Array(16);
    crypto.getRandomValues(bytes);

    let chars = "";

    for (const b of bytes)
        chars += KEY_ALPHABET[b & 31];

    return chars.slice(0, 4) + "-" +
        chars.slice(4, 8) + "-" +
        chars.slice(8, 12) + "-" +
        chars.slice(12, 16);
}

function licenseKvKey(key)
{
    return "key:" + key.trim().toUpperCase();
}

function isAdmin(env, userId)
{
    if (!env.ADMIN_IDS || !userId)
        return false;

    const ids = String(env.ADMIN_IDS).split(",").map((s) => s.trim()).filter(Boolean);
    return ids.includes(String(userId));
}

function ephemeral(content)
{
    return jsonResponse({
        type: 4,
        data: {
            content,
            flags: 64,
        },
    });
}

// Basic fixed-window rate limiting backed by KV (best-effort, slows brute force).
async function checkRateLimit(env, rlKey, limit, windowSeconds)
{
    const nowBucket = Math.floor(Date.now() / 1000 / windowSeconds);
    const kvKey = "rl:" + rlKey + ":" + nowBucket;
    const current = parseInt(await env.LICENSES.get(kvKey) || "0", 10);

    if (current >= limit)
        return false;

    await env.LICENSES.put(kvKey, String(current + 1), { expirationTtl: windowSeconds + 5 });
    return true;
}

function clientIp(request)
{
    return request.headers.get("CF-Connecting-IP") ||
        request.headers.get("X-Forwarded-For")?.split(",")[0]?.trim() ||
        "unknown";
}

function parseSemver(version)
{
    if (typeof version !== "string")
        return null;

    const match = version.trim().replace(/^[vV]/, "").match(/^(\d+)\.(\d+)\.(\d+)$/);

    if (!match)
        return null;

    return [
        parseInt(match[1], 10),
        parseInt(match[2], 10),
        parseInt(match[3], 10),
    ];
}

function compareSemver(a, b)
{
    const pa = parseSemver(a);
    const pb = parseSemver(b);

    if (!pa || !pb)
        return 0;

    for (let i = 0; i < 3; i++)
    {
        if (pa[i] > pb[i])
            return 1;

        if (pa[i] < pb[i])
            return -1;
    }

    return 0;
}

async function signMessage(env, message)
{
    const keyObj = await getSigningKey(env.SIGNING_PRIVATE_KEY);
    const sig = await crypto.subtle.sign({ name: "Ed25519" }, keyObj, encoder.encode(message));
    return bytesToHex(new Uint8Array(sig));
}

// Shared license validation used by both /validate and /update.
async function validateLicensePayload(request, env, body)
{
    const key = typeof body.key === "string" ? body.key.trim().toUpperCase() : "";
    const hwid = typeof body.hwid === "string" ? body.hwid.trim() : "";
    const nonce = typeof body.nonce === "string" ? body.nonce.trim() : "";

    if (!key || !hwid || !nonce || key.length > 64 || hwid.length > 128 || nonce.length > 128)
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "bad_request" }, 400) };

    if (!/^[A-Z0-9-]{1,64}$/.test(key) || !/^[0-9a-fA-F]{1,128}$/.test(hwid) || !/^[0-9a-fA-F]{1,128}$/.test(nonce))
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "bad_request" }, 400) };

    // Rate limits: 30/min per IP, 10/min per key.
    const ip = clientIp(request);

    if (!await checkRateLimit(env, "ip:" + ip, 30, 60))
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "rate_limited" }, 429) };

    if (!await checkRateLimit(env, "k:" + key, 10, 60))
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "rate_limited" }, 429) };

    const kvKey = licenseKvKey(key);
    const record = await env.LICENSES.get(kvKey, "json");

    if (!record)
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "unknown_key" }) };

    if (record.revoked)
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "revoked" }) };

    const nowSec = Math.floor(Date.now() / 1000);

    if (record.expiresAt !== null && record.expiresAt !== undefined && nowSec > record.expiresAt)
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "expired" }) };

    if (!record.hwid)
    {
        // First use binds the key to this machine.
        record.hwid = hwid;
        await env.LICENSES.put(kvKey, JSON.stringify(record));
    }
    else if (record.hwid !== hwid)
    {
        return { ok: false, response: jsonResponse({ status: "invalid", reason: "hwid_mismatch" }) };
    }

    const expiresAtNum = (record.expiresAt === null || record.expiresAt === undefined) ? 0 : record.expiresAt;
    return {
        ok: true,
        key,
        hwid,
        nonce,
        ts: nowSec,
        expiresAt: expiresAtNum,
        record,
    };
}

async function handleValidate(request, env)
{
    let body;

    try
    {
        body = await request.json();
    }
    catch (err)
    {
        return jsonResponse({ status: "invalid", reason: "bad_request" }, 400);
    }

    if (!body || typeof body !== "object")
        return jsonResponse({ status: "invalid", reason: "bad_request" }, 400);

    const validated = await validateLicensePayload(request, env, body);

    if (!validated.ok)
        return validated.response;

    const { key, hwid, nonce, ts, expiresAt } = validated;
    const message = "valid|" + key + "|" + hwid + "|" + nonce + "|" + String(ts) + "|" + String(expiresAt);

    let signatureHex;

    try
    {
        signatureHex = await signMessage(env, message);
    }
    catch (err)
    {
        return jsonResponse({ status: "invalid", reason: "server_error" }, 500);
    }

    return jsonResponse({
        status: "valid",
        key,
        hwid,
        nonce,
        ts,
        expiresAt,
        sig: signatureHex,
    });
}

async function handleUpdate(request, env)
{
    let body;

    try
    {
        body = await request.json();
    }
    catch (err)
    {
        return jsonResponse({ status: "invalid", reason: "bad_request" }, 400);
    }

    if (!body || typeof body !== "object")
        return jsonResponse({ status: "invalid", reason: "bad_request" }, 400);

    const currentVersion = typeof body.currentVersion === "string" ? body.currentVersion.trim() : "";

    if (!currentVersion || currentVersion.length > 32 || !parseSemver(currentVersion))
        return jsonResponse({ status: "invalid", reason: "bad_request" }, 400);

    const validated = await validateLicensePayload(request, env, body);

    if (!validated.ok)
        return validated.response;

    const { nonce, ts } = validated;
    const manifest = await env.LICENSES.get("release:latest", "json");

    const hasNewerRelease =
        manifest &&
        typeof manifest.version === "string" &&
        typeof manifest.url === "string" &&
        typeof manifest.sha256 === "string" &&
        typeof manifest.size === "number" &&
        typeof manifest.releaseSig === "string" &&
        parseSemver(manifest.version) &&
        compareSemver(manifest.version, currentVersion) > 0;

    if (!hasNewerRelease)
    {
        const message = "current|" + nonce + "|" + String(ts);
        let signatureHex;

        try
        {
            signatureHex = await signMessage(env, message);
        }
        catch (err)
        {
            return jsonResponse({ status: "invalid", reason: "server_error" }, 500);
        }

        return jsonResponse({
            status: "current",
            nonce,
            ts,
            sig: signatureHex,
        });
    }

    const message = "update|" + manifest.version + "|" + manifest.url + "|" +
        manifest.sha256 + "|" + nonce + "|" + String(ts);

    let signatureHex;

    try
    {
        signatureHex = await signMessage(env, message);
    }
    catch (err)
    {
        return jsonResponse({ status: "invalid", reason: "server_error" }, 500);
    }

    return jsonResponse({
        status: "ok",
        manifest: {
            version: manifest.version,
            url: manifest.url,
            sha256: manifest.sha256,
            size: manifest.size,
            notes: typeof manifest.notes === "string" ? manifest.notes : "",
            releaseSig: manifest.releaseSig,
        },
        nonce,
        ts,
        sig: signatureHex,
    });
}

// ---------------------------------------------------------------------------
// Discord interactions
// ---------------------------------------------------------------------------

function getOption(interaction, name)
{
    const options = interaction?.data?.options || [];
    return options.find((o) => o.name === name)?.value;
}

function formatExpiry(expiresAt)
{
    if (expiresAt === null || expiresAt === undefined)
        return "lifetime";

    const d = new Date(expiresAt * 1000);
    return d.toISOString().slice(0, 10) + " (" + String(expiresAt) + ")";
}

function keyStatusLine(key, record)
{
    const nowSec = Math.floor(Date.now() / 1000);
    let status = "valid";

    if (record.revoked)
        status = "revoked";
    else if (record.expiresAt !== null && record.expiresAt !== undefined && nowSec > record.expiresAt)
        status = "expired";

    const bound = record.hwid ? "bound" : "unbound";
    return "`" + key + "` " + status + " | " + formatExpiry(record.expiresAt) + " | " + bound;
}

async function handleDiscord(request, env)
{
    if (!env.DISCORD_PUBLIC_KEY)
        return new Response("Discord not configured", { status: 500 });

    const { valid, rawBody } = await verifyDiscordRequest(request, env.DISCORD_PUBLIC_KEY);

    if (!valid)
        return new Response("Invalid request signature", { status: 401 });

    let interaction;

    try
    {
        interaction = JSON.parse(decoder.decode(rawBody));
    }
    catch (err)
    {
        return new Response("Invalid JSON", { status: 400 });
    }

    // PING -> PONG (required for the endpoint URL verification).
    if (interaction.type === 1)
        return jsonResponse({ type: 1 });

    if (interaction.type !== 2)
        return ephemeral("Unsupported interaction type.");

    const userId = interaction?.member?.user?.id || interaction?.user?.id || "";

    if (!isAdmin(env, userId))
        return ephemeral("Not allowed.");

    const command = interaction?.data?.name || "";

    try
    {
        if (command === "genkey")
        {
            const days = getOption(interaction, "days");
            const count = Math.min(Math.max(parseInt(getOption(interaction, "count") ?? "1", 10) || 1, 1), 10);
            const note = String(getOption(interaction, "note") ?? "").slice(0, 120);
            const nowSec = Math.floor(Date.now() / 1000);

            let expiresAt = null;

            if (days !== undefined && days !== null)
            {
                const d = parseInt(days, 10);

                if (!Number.isFinite(d) || d < 1 || d > 3650)
                    return ephemeral("`days` must be between 1 and 3650, or omitted for lifetime.");

                expiresAt = nowSec + d * 86400;
            }

            const lines = [];

            for (let i = 0; i < count; i++)
            {
                const newKey = randomKey();
                const record = {
                    created: nowSec,
                    expiresAt,
                    revoked: false,
                    hwid: null,
                    note,
                };

                await env.LICENSES.put(licenseKvKey(newKey), JSON.stringify(record));
                lines.push("`" + newKey + "`");
            }

            const expiryText = expiresAt === null ? "lifetime" : formatExpiry(expiresAt);
            return ephemeral("Created " + String(count) + " key(s) (" + expiryText + ")" +
                (note ? " note: " + note : "") + ":\n" + lines.join("\n"));
        }

        if (command === "revoke")
        {
            const key = String(getOption(interaction, "key") ?? "").trim().toUpperCase();

            if (!key)
                return ephemeral("Usage: /revoke key:<KEY>");

            const kvKey = licenseKvKey(key);
            const record = await env.LICENSES.get(kvKey, "json");

            if (!record)
                return ephemeral("Unknown key `" + key + "`.");

            record.revoked = true;
            await env.LICENSES.put(kvKey, JSON.stringify(record));
            return ephemeral("Revoked `" + key + "`.");
        }

        if (command === "resethwid")
        {
            const key = String(getOption(interaction, "key") ?? "").trim().toUpperCase();

            if (!key)
                return ephemeral("Usage: /resethwid key:<KEY>");

            const kvKey = licenseKvKey(key);
            const record = await env.LICENSES.get(kvKey, "json");

            if (!record)
                return ephemeral("Unknown key `" + key + "`.");

            record.hwid = null;
            await env.LICENSES.put(kvKey, JSON.stringify(record));
            return ephemeral("Cleared hardware binding for `" + key + "`.");
        }

        if (command === "keyinfo")
        {
            const key = String(getOption(interaction, "key") ?? "").trim().toUpperCase();

            if (!key)
                return ephemeral("Usage: /keyinfo key:<KEY>");

            const record = await env.LICENSES.get(licenseKvKey(key), "json");

            if (!record)
                return ephemeral("Unknown key `" + key + "`.");

            const created = new Date(record.created * 1000).toISOString().slice(0, 10);
            return ephemeral("`" + key + "`\nstatus: " + keyStatusLine(key, record).split("`")[2].trim() +
                "\ncreated: " + created +
                "\nexpires: " + formatExpiry(record.expiresAt) +
                "\nbound: " + (record.hwid ? "yes" : "no") +
                (record.note ? "\nnote: " + record.note : ""));
        }

        if (command === "listkeys")
        {
            const list = await env.LICENSES.list({ prefix: "key:", limit: 25 });

            if (list.keys.length === 0)
                return ephemeral("No keys stored.");

            const lines = [];

            for (const entry of list.keys)
            {
                const shortKey = entry.name.slice(4);
                const record = await env.LICENSES.get(entry.name, "json");

                if (record)
                    lines.push(keyStatusLine(shortKey, record));
            }

            return ephemeral("Keys (" + String(lines.length) + " shown, up to 25):\n" + lines.join("\n"));
        }

        return ephemeral("Unknown command.");
    }
    catch (err)
    {
        return ephemeral("Server error. Try again.");
    }
}

export default {
    async fetch(request, env)
    {
        const url = new URL(request.url);

        if (request.method === "GET" && (url.pathname === "/" || url.pathname === "/health"))
            return jsonResponse({ ok: true, service: "dribtard-license-server" });

        if (url.pathname === "/validate" && request.method === "POST")
            return handleValidate(request, env);

        if (url.pathname === "/update" && request.method === "POST")
            return handleUpdate(request, env);

        if (url.pathname === "/discord" && request.method === "POST")
            return handleDiscord(request, env);

        return new Response("Not found", { status: 404 });
    },
};
