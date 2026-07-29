import { generateKeyPairSync } from "node:crypto";

export interface TestRsaKeyPair {
  privateKeyPem: string;
  publicKeyPem: string;
}

export function generateTestRsaKeyPair(): TestRsaKeyPair {
  const { privateKey, publicKey } = generateKeyPairSync("rsa", { modulusLength: 2048 });
  return {
    privateKeyPem: privateKey.export({ type: "pkcs8", format: "pem" }).toString(),
    publicKeyPem: publicKey.export({ type: "spki", format: "pem" }).toString()
  };
}
