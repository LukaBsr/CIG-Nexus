import { createHash } from "node:crypto";

import { describe, expect, it } from "vitest";

import { generateCodeChallenge, generateCodeVerifier, generateState } from "./pkce";

describe("generateCodeVerifier", () => {
  it("produces a string within the 43-128 char range required by PKCE", () => {
    const verifier = generateCodeVerifier();
    expect(verifier.length).toBeGreaterThanOrEqual(43);
    expect(verifier.length).toBeLessThanOrEqual(128);
  });

  it("produces different values on each call", () => {
    expect(generateCodeVerifier()).not.toBe(generateCodeVerifier());
  });
});

describe("generateCodeChallenge", () => {
  it("computes BASE64URL(SHA256(verifier))", () => {
    const verifier = "test-verifier-value";
    const expected = createHash("sha256").update(verifier).digest("base64url");
    expect(generateCodeChallenge(verifier)).toBe(expected);
  });

  it("is deterministic for the same verifier", () => {
    const verifier = generateCodeVerifier();
    expect(generateCodeChallenge(verifier)).toBe(generateCodeChallenge(verifier));
  });
});

describe("generateState", () => {
  it("is not reused as the code verifier and differs per call", () => {
    const state = generateState();
    expect(state).not.toBe(generateCodeVerifier());
    expect(generateState()).not.toBe(generateState());
  });
});
