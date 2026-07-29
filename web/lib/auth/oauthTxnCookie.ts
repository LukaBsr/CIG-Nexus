import { SignJWT, jwtVerify } from "jose";

import { authEnv } from "./env";

// design doc §4 step 4: cookie is "signed/encrypted so a tampered cookie is
// rejected rather than trusted." Implemented as a symmetric-signed (HS256)
// JWT — this cookie is only ever written and read by this same Next.js
// process, so there's no cross-service verification need that would call
// for asymmetric keys the way the application session JWT does (§6).
export const OAUTH_TXN_COOKIE = "oauth_txn";
const OAUTH_TXN_TTL_SECONDS = 5 * 60;

interface OAuthTxnPayload {
  codeVerifier: string;
  state: string;
}

export async function signOAuthTxn(payload: OAuthTxnPayload): Promise<string> {
  const secret = new TextEncoder().encode(authEnv.oauthTxnSecret);
  return new SignJWT({ code_verifier: payload.codeVerifier, state: payload.state })
    .setProtectedHeader({ alg: "HS256" })
    .setIssuedAt()
    .setExpirationTime(`${OAUTH_TXN_TTL_SECONDS}s`)
    .sign(secret);
}

export async function verifyOAuthTxn(token: string): Promise<OAuthTxnPayload | null> {
  try {
    const secret = new TextEncoder().encode(authEnv.oauthTxnSecret);
    const { payload } = await jwtVerify(token, secret, { algorithms: ["HS256"] });
    if (typeof payload.code_verifier !== "string" || typeof payload.state !== "string") {
      return null;
    }
    return { codeVerifier: payload.code_verifier, state: payload.state };
  } catch {
    return null;
  }
}

export const OAUTH_TXN_COOKIE_OPTIONS = {
  httpOnly: true,
  // §9.1: secure cookies. Conditioned on NODE_ENV (not unconditional) because
  // this stack currently terminates plain HTTP in local/docker-compose dev
  // (shared/protocol/README.md, "Security and Limits": no TLS) — an
  // unconditional Secure flag would make the cookie unsendable there.
  secure: process.env.NODE_ENV === "production",
  sameSite: "lax" as const,
  path: "/",
  maxAge: OAUTH_TXN_TTL_SECONDS
};
