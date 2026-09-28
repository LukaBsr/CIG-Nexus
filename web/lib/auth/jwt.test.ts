import { readFileSync } from "node:fs";

import { decodeJwt, decodeProtectedHeader, importSPKI, jwtVerify } from "jose";
import { beforeAll, describe, expect, it } from "vitest";

import { generateTestRsaKeyPair } from "../../test/rsaKeys";
import { writeTempPemFile } from "../../test/writeTempPemFile";
import {
  ACCESS_JWT_AUDIENCE,
  ACCESS_JWT_ISSUER,
  ACCESS_JWT_TTL_SECONDS,
  issueAccessJwt
} from "./jwt";

let publicKeyPem: string;

beforeAll(() => {
  const { privateKeyPem, publicKeyPem: pub } = generateTestRsaKeyPair();
  process.env.SESSION_JWT_PRIVATE_KEY_PATH = writeTempPemFile(privateKeyPem, "private.pem");
  publicKeyPem = pub;
});

describe("issueAccessJwt", () => {
  it("issues an RS256 JWT verifiable with the corresponding public key, carrying the given claims", async () => {
    const { token, expiresAt } = await issueAccessJwt({
      sub: "u_11111111-1111-1111-1111-111111111111",
      discordId: "999",
      username: "web_user",
      sid: "22222222-2222-2222-2222-222222222222"
    });

    const publicKey = await importSPKI(publicKeyPem, "RS256");
    const { payload, protectedHeader } = await jwtVerify(token, publicKey, {
      algorithms: ["RS256"],
      issuer: "cig-nexus-web",
      audience: "cig-nexus-server"
    });

    expect(protectedHeader.alg).toBe("RS256");
    expect(payload.sub).toBe("u_11111111-1111-1111-1111-111111111111");
    expect(payload.discord_id).toBe("999");
    expect(payload.username).toBe("web_user");
    expect(payload.sid).toBe("22222222-2222-2222-2222-222222222222");
    expect(payload.exp).toBeDefined();
    expect(expiresAt.getTime() / 1000).toBeCloseTo(payload.exp as number, 0);
  });

  it("sets an expiry between 10 and 15 minutes out (design doc §6)", async () => {
    const before = Date.now();
    const { expiresAt } = await issueAccessJwt({
      sub: "u_1",
      discordId: "1",
      username: "u",
      sid: "s"
    });
    const ttlSeconds = (expiresAt.getTime() - before) / 1000;
    expect(ttlSeconds).toBeGreaterThanOrEqual(10 * 60 - 5);
    expect(ttlSeconds).toBeLessThanOrEqual(15 * 60 + 5);
  });

  it("rejects verification against the wrong audience", async () => {
    const { token } = await issueAccessJwt({ sub: "u_1", discordId: "1", username: "u", sid: "s" });
    const publicKey = await importSPKI(publicKeyPem, "RS256");
    await expect(
      jwtVerify(token, publicKey, { algorithms: ["RS256"], audience: "someone-else" })
    ).rejects.toThrow();
  });
});

// Contract with the C++ verifier (server/src/auth/JwtVerifier.cpp): it pins
// alg to RS256, requires exp (non-negative integer), aud, iss, and the four
// string claims sub/discord_id/username/sid. These assertions are made
// against the signer's own exported constants, so a change to what the
// signer emits fails here instead of surfacing as INVALID_SESSION at IDENTIFY.
describe("issueAccessJwt's contract with the C++ verifier", () => {
  const claims = {
    sub: "u_11111111-1111-1111-1111-111111111111",
    discordId: "999",
    username: "web_user",
    sid: "22222222-2222-2222-2222-222222222222"
  };

  it("emits an RS256 header and exactly the claims the verifier requires", async () => {
    const before = Math.floor(Date.now() / 1000);
    const { token } = await issueAccessJwt(claims);

    expect(decodeProtectedHeader(token)).toEqual({ alg: "RS256" });

    const payload = decodeJwt(token);
    expect(Object.keys(payload).sort()).toEqual(
      ["aud", "discord_id", "exp", "iat", "iss", "sid", "sub", "username"].sort()
    );
    expect(payload.iss).toBe(ACCESS_JWT_ISSUER);
    expect(payload.aud).toBe(ACCESS_JWT_AUDIENCE);
    expect(payload.sub).toBe(claims.sub);
    expect(payload.discord_id).toBe(claims.discordId);
    expect(payload.username).toBe(claims.username);
    expect(payload.sid).toBe(claims.sid);

    expect(Number.isInteger(payload.exp)).toBe(true);
    expect(payload.exp as number).toBeGreaterThan(0);
    expect(Number.isInteger(payload.iat)).toBe(true);
    expect(payload.exp as number).toBe((payload.iat as number) + ACCESS_JWT_TTL_SECONDS);
    expect(payload.iat as number).toBeGreaterThanOrEqual(before);
  });

  it("keeps the committed frozen fixture in step with the signer", async () => {
    // server/tests/fixtures/jwt: one token produced by the real signer with a
    // throwaway key (discarded) — the C++ test verifies it with an injected
    // clock. If the signer's header, claim set, iss or aud change, this
    // fails until the fixture is regenerated (see the fixture's README).
    const dir = new URL("../../../server/tests/fixtures/jwt/", import.meta.url);
    const fixtureToken = readFileSync(new URL("web-signer-token.txt", dir), "utf8").trim();
    const fixturePublicKeyPem = readFileSync(new URL("public.pem", dir), "utf8");

    const { token: currentToken } = await issueAccessJwt(claims);
    expect(decodeProtectedHeader(fixtureToken)).toEqual(decodeProtectedHeader(currentToken));
    expect(Object.keys(decodeJwt(fixtureToken)).sort()).toEqual(
      Object.keys(decodeJwt(currentToken)).sort()
    );

    const fixturePayload = decodeJwt(fixtureToken);
    const publicKey = await importSPKI(fixturePublicKeyPem, "RS256");
    await jwtVerify(fixtureToken, publicKey, {
      algorithms: ["RS256"],
      issuer: ACCESS_JWT_ISSUER,
      audience: ACCESS_JWT_AUDIENCE,
      currentDate: new Date(((fixturePayload.iat as number) + 60) * 1000)
    });
    expect(fixturePayload.exp).toBe((fixturePayload.iat as number) + ACCESS_JWT_TTL_SECONDS);
  });
});
