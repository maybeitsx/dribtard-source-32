// Generates a dedicated Ed25519 keypair for signing release binaries.
//
// IMPORTANT: This release signing key is completely separate from the
// Cloudflare Worker's SIGNING_PRIVATE_KEY. Keep release_private.pem local and
// NEVER upload it to Cloudflare or commit it to git.
//
// Usage: node release-tools/gen_release_key.mjs
//
// Outputs:
//   - Writes the private key to release-tools/release_private.pem (mode 0600)
//   - Prints the public key as raw 32 bytes hex
//   - Prints the public key as a C++ array for LicenseConfig.hpp (kReleasePublicKey)
//
// Uses only Node's built-in crypto and fs modules. No dependencies.

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const privateKeyPath = path.join(__dirname, "release_private.pem");

if (fs.existsSync(privateKeyPath) && !process.argv.includes("--force"))
{
    console.error("Refusing to overwrite existing key: " + privateKeyPath);
    console.error("Pass --force if you intentionally want to rotate the release signing key.");
    process.exit(1);
}

const { publicKey, privateKey } = crypto.generateKeyPairSync("ed25519");

const pem = privateKey.export({ format: "pem", type: "pkcs8" });
fs.writeFileSync(privateKeyPath, pem, { encoding: "utf8", mode: 0o600 });

const jwk = publicKey.export({ format: "jwk" });
const rawPub = Buffer.from(jwk.x, "base64url");
const pubHex = rawPub.toString("hex");

const cArray = Array.from(rawPub)
    .map((b) => "0x" + b.toString(16).padStart(2, "0"))
    .join(", ");

console.log("Saved release private key (NEVER upload to Cloudflare or commit to git):");
console.log("  " + privateKeyPath);
console.log();
console.log("Release public key (raw 32 bytes, hex):");
console.log("  " + pubHex);
console.log();
console.log("Release public key (C++ array for External Macro/src/Licensing/LicenseConfig.hpp -> kReleasePublicKey):");
console.log("    " + cArray);
console.log();
console.log("Next steps:");
console.log("  1. Paste the C++ array above into External Macro/src/Licensing/LicenseConfig.hpp (kReleasePublicKey)");
console.log("  2. Keep release-tools/release_private.pem backed up offline and out of source control");
