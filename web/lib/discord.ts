import { authEnv } from "./auth/env";

// This integration never calls Discord's API again after the initial login
// (design doc §1) — these two calls are the entire surface.

export interface DiscordTokenResponse {
  access_token: string;
  refresh_token: string;
  expires_in: number;
  scope: string;
  token_type: string;
}

export interface DiscordUser {
  id: string;
  username: string;
  global_name: string | null;
  avatar: string | null;
}

export async function exchangeCodeForToken(
  code: string,
  codeVerifier: string
): Promise<DiscordTokenResponse> {
  const response = await fetch("https://discord.com/api/oauth2/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      grant_type: "authorization_code",
      code,
      redirect_uri: authEnv.discordRedirectUri,
      code_verifier: codeVerifier,
      client_id: authEnv.discordClientId,
      client_secret: authEnv.discordClientSecret
    })
  });

  if (!response.ok) {
    throw new Error(`Discord token exchange failed: ${response.status}`);
  }

  return (await response.json()) as DiscordTokenResponse;
}

export async function fetchDiscordUser(accessToken: string): Promise<DiscordUser> {
  const response = await fetch("https://discord.com/api/users/@me", {
    headers: { Authorization: `Bearer ${accessToken}` }
  });

  if (!response.ok) {
    throw new Error(`Discord profile fetch failed: ${response.status}`);
  }

  return (await response.json()) as DiscordUser;
}
