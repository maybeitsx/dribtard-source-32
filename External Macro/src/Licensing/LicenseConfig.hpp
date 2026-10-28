#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Licensing
{

// ---------------------------------------------------------------------------
// License server configuration.
//
// Fill these in after deploying the Worker (see license-server/README.md):
//   1. Run `node generate_keys.mjs` in license-server/ to create a keypair.
//   2. Paste the public key C array below (replacing the zeros).
//   3. Set kServerUrl to your Worker's public URL, e.g.
//      "https://dribtard-license-server.<you>.workers.dev"
// ---------------------------------------------------------------------------

inline const std::string kServerUrl =
    "https://dribtard-license-server.kasonjonesrios.workers.dev";

// 32-byte Ed25519 server public key (raw bytes). PLACEHOLDER: all zeros means
// "not configured" - every validation fails until the real key is pasted in.
// Get the values from `node generate_keys.mjs` in license-server/.
inline const std::array<std::uint8_t, 32> kServerPublicKey = {{
     0x28, 0xff, 0xc3, 0x7f, 0xaa, 0xad, 0x02, 0x8d, 0x17, 0xa2, 0x82, 0x47, 0x86, 0xb1, 0x19, 0x47, 0x1d, 0x8f, 0xb5, 0x40, 0xa9, 0x31, 0x83, 0x9b, 0x54, 0x9f, 0x06, 0xf6, 0xb0, 0xe5, 0x90, 0x61
}};

// 32-byte Ed25519 release signing public key (raw bytes). This key is DISTINCT
// from kServerPublicKey above: the corresponding private key lives ONLY in
// release-tools/release_private.pem on your developer machine and is NEVER
// uploaded to Cloudflare.
inline const std::array<std::uint8_t, 32> kReleasePublicKey = {{
    0xef, 0x46, 0x14, 0x4c, 0xb4, 0xa0, 0xd6, 0x09,
    0x68, 0x5a, 0xe6, 0x27, 0x46, 0xc9, 0xa5, 0x7b,
    0x28, 0xd7, 0xde, 0xba, 0xf6, 0x18, 0x46, 0x62,
    0x76, 0x78, 0xb2, 0x6f, 0xf5, 0x66, 0x94, 0x11
}};

} // namespace Licensing
