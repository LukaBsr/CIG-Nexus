import { type NextRequest, NextResponse } from "next/server";

import { clientIp } from "@/lib/auth/clientIp";
import { authEnv } from "@/lib/auth/env";
import { OAUTH_TXN_COOKIE, OAUTH_TXN_COOKIE_OPTIONS, signOAuthTxn } from "@/lib/auth/oauthTxnCookie";
import { generateCodeChallenge, generateCodeVerifier, generateState } from "@/lib/auth/pkce";
import { checkRateLimit } from "@/lib/auth/rateLimit";

// design doc §9.1: per-IP sliding window. No specific threshold is given
// in the design doc; 10 requests/minute is a starting point sized to allow
// normal retry/back-and-forth during login without materially slowing down
// a credential-stuffing-style abuse pattern against these routes.
const RATE_LIMIT = { windowMs: 60_000, limit: 10 };

// design doc §4, GET /api/auth/discord/login
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!(await checkRateLimit("oauth-login", clientIp(request), RATE_LIMIT))) {
    return NextResponse.json({ error: "rate_limited" }, { status: 429 });
  }

  const codeVerifier = generateCodeVerifier();
  const codeChallenge = generateCodeChallenge(codeVerifier);
  const state = generateState();

  const txn = await signOAuthTxn({ codeVerifier, state });

  const authorizeUrl = new URL("https://discord.com/oauth2/authorize");
  authorizeUrl.searchParams.set("client_id", authEnv.discordClientId);
  authorizeUrl.searchParams.set("redirect_uri", authEnv.discordRedirectUri);
  authorizeUrl.searchParams.set("response_type", "code");
  authorizeUrl.searchParams.set("scope", "identify");
  authorizeUrl.searchParams.set("state", state);
  authorizeUrl.searchParams.set("code_challenge", codeChallenge);
  authorizeUrl.searchParams.set("code_challenge_method", "S256");

  const response = NextResponse.redirect(authorizeUrl);
  response.cookies.set(OAUTH_TXN_COOKIE, txn, OAUTH_TXN_COOKIE_OPTIONS);
  return response;
}
