import { importPKCS8, SignJWT } from "jose";

import { authEnv } from "./env";

// design doc §6: short TTL, RS256, private key never leaves Next.js.
const ACCESS_JWT_TTL_SECONDS = 15 * 60;
const ISSUER = "cig-nexus-web";
const AUDIENCE = "cig-nexus-server";

export interface AccessJwtClaims {
  sub: string; // "u_<uuid>"
  discordId: string;
  username: string;
  sid: string;
}

export interface IssuedAccessJwt {
  token: string;
  expiresAt: Date;
}

export async function issueAccessJwt(claims: AccessJwtClaims): Promise<IssuedAccessJwt> {
  const privateKey = await importPKCS8(authEnv.sessionJwtPrivateKey, "RS256");
  const now = Math.floor(Date.now() / 1000);
  const exp = now + ACCESS_JWT_TTL_SECONDS;

  const token = await new SignJWT({
    discord_id: claims.discordId,
    username: claims.username,
    sid: claims.sid
  })
    .setProtectedHeader({ alg: "RS256" })
    .setSubject(claims.sub)
    .setIssuedAt(now)
    .setExpirationTime(exp)
    .setIssuer(ISSUER)
    .setAudience(AUDIENCE)
    .sign(privateKey);

  return { token, expiresAt: new Date(exp * 1000) };
}
