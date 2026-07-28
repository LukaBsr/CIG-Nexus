import { createHash, randomBytes } from "node:crypto";

// design doc §4 step 1: 43-128 char string. 32 random bytes -> 43 base64url
// characters, at the minimum of the allowed range.
export function generateCodeVerifier(): string {
  return randomBytes(32).toString("base64url");
}

// design doc §4 step 2: BASE64URL(SHA256(code_verifier)).
export function generateCodeChallenge(verifier: string): string {
  return createHash("sha256").update(verifier).digest("base64url");
}

// design doc §4 step 3: separate random value, not reused as the verifier.
export function generateState(): string {
  return randomBytes(32).toString("base64url");
}
