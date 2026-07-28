import { randomBytes } from "node:crypto";

import { beforeAll, describe, expect, it } from "vitest";

import { signOAuthTxn, verifyOAuthTxn } from "./oauthTxnCookie";

beforeAll(() => {
  process.env.OAUTH_TXN_SECRET = randomBytes(32).toString("hex");
});

describe("signOAuthTxn / verifyOAuthTxn", () => {
  it("round-trips the code verifier and state", async () => {
    const token = await signOAuthTxn({ codeVerifier: "verifier-abc", state: "state-xyz" });
    const payload = await verifyOAuthTxn(token);
    expect(payload).toEqual({ codeVerifier: "verifier-abc", state: "state-xyz" });
  });

  it("rejects a tampered token", async () => {
    const token = await signOAuthTxn({ codeVerifier: "verifier-abc", state: "state-xyz" });
    const tampered = `${token.slice(0, -4)}abcd`;
    await expect(verifyOAuthTxn(tampered)).resolves.toBeNull();
  });

  it("rejects a token signed with a different secret", async () => {
    const token = await signOAuthTxn({ codeVerifier: "verifier-abc", state: "state-xyz" });
    process.env.OAUTH_TXN_SECRET = randomBytes(32).toString("hex");
    await expect(verifyOAuthTxn(token)).resolves.toBeNull();
  });

  it("rejects garbage input", async () => {
    await expect(verifyOAuthTxn("not-a-jwt")).resolves.toBeNull();
  });
});
