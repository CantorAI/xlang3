// Independent non-CPython AES-GCM reference using Node's crypto implementation.
import { createCipheriv } from 'node:crypto';
import { writeFileSync } from 'node:fs';
const vectors = [];
for (const keyLength of [16, 24, 32]) {
  for (const nonceLength of [8, 12, 128]) {
    for (const length of [0, 1, 33]) {
      const key = Buffer.from(Array.from({ length: keyLength }, (_, i) => i));
      const nonce = Buffer.from(Array.from({ length: nonceLength }, (_, i) => (i * 3) % 256));
      const data = Buffer.from(Array.from({ length }, (_, i) => (i * 7) % 256));
      const aad = length ? Buffer.from([0, 255, 1, 128]) : Buffer.alloc(0);
      const cipher = createCipheriv(`aes-${keyLength * 8}-gcm`, key, nonce);
      cipher.setAAD(aad);
      const ciphertext = Buffer.concat([cipher.update(data), cipher.final(), cipher.getAuthTag()]);
      vectors.push(Object.fromEntries(Object.entries({ key, nonce, data, aad, ciphertext }).map(([name, value]) => [name, value.toString('hex')])));
    }
  }
}
writeFileSync(process.argv[2], JSON.stringify(vectors, null, 2) + '\n');
console.log(`Generated ${vectors.length} independent AES-GCM vectors`);
