import { NextResponse } from "next/server";

import { authEnv } from "@/lib/auth/env";
import { OAUTH_TXN_COOKIE, OAUTH_TXN_COOKIE_OPTIONS, signOAuthTxn } from "@/lib/auth/oauthTxnCookie";
import { generateCodeChallenge, generateCodeVerifier, generateState } from "@/lib/auth/pkce";

// design doc §4, GET /api/auth/discord/login
export async function GET(): Promise<NextResponse> {
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
