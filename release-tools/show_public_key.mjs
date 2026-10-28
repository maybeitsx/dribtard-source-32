import { readFileSync } from 'node:fs';
import { createPublicKey } from 'node:crypto';

const pem = readFileSync(new URL('./release_private.pem', import.meta.url));
const jwk = createPublicKey(pem).export({ format: 'jwk' });
const raw = Buffer.from(jwk.x, 'base64url');

console.log('hex:', raw.toString('hex'));
console.log('C array:');
console.log([...raw].map(b => '0x' + b.toString(16).padStart(2, '0')).join(', '));