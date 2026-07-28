import { importSPKI, jwtVerify } from "jose";
import { beforeAll, describe, expect, it } from "vitest";

import { generateTestRsaKeyPair } from "../../test/rsaKeys";
import { issueAccessJwt } from "./jwt";

let publicKeyPem: string;

beforeAll(() => {
  const { privateKeyPem, publicKeyPem: pub } = generateTestRsaKeyPair();
  process.env.SESSION_JWT_PRIVATE_KEY = privateKeyPem;
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
