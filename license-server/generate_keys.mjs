// Generates an Ed25519 keypair for signing license validation responses.
//
// Usage: node generate_keys.mjs
//
// Prints:
//   - private key as PKCS#8 DER base64  -> use as the SIGNING_PRIVATE_KEY wrangler secret
//   - public key as raw 32 bytes hex    -> hard to guess; only the public half ships
//   - public key as a C++ array         -> paste into LicenseConfig.hpp
//
// Uses only Node's built-in crypto module. No dependencies.

import crypto from "node:crypto";

const { publicKey, privateKey } = crypto.generateKeyPairSync("ed25519");

// PKCS#8 DER base64 for the Worker secret (imported with WebCrypto "pkcs8").
const pkcs8Der = privateKey.export({ format: "der", type: "pkcs8" });
const pkcs8Base64 = pkcs8Der.toString("base64");

// Raw 32-byte public key (JWK "x" is base64url of the raw key).
const jwk = publicKey.export({ format: "jwk" });
const rawPub = Buffer.from(jwk.x, "base64url");
const pubHex = rawPub.toString("hex");

const cArray = Array.from(rawPub)
    .map((b) => "0x" + b.toString(16).padStart(2, "0"))
    .join(", ");

console.log("SIGNING_PRIVATE_KEY (wrangler secret, PKCS#8 base64):");
console.log(pkcs8Base64);
console.log();
console.log("Server public key (raw 32 bytes, hex):");
console.log(pubHex);
console.log();
console.log("Server public key (C++ array for LicenseConfig.hpp):");
console.log("    " + cArray);
console.log();
console.log("Next steps:");
console.log("  1. npx wrangler secret put SIGNING_PRIVATE_KEY   (paste the base64 above)");
console.log("  2. Paste the C++ array into External Macro/src/Licensing/LicenseConfig.hpp");
