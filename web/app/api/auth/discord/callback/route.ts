import { type NextRequest, NextResponse } from "next/server";

import { THEME_COOKIE, THEME_COOKIE_OPTIONS, THEME_SYNC_COOKIE } from "@/lib/appearance/cookie";
import { resolveThemeId } from "@/lib/appearance/themes";
import { clientIp as rateLimitClientIp, trustedRemoteAddress } from "@/lib/auth/clientIp";
import { constantTimeEqual } from "@/lib/auth/constantTimeEqual";
import { OAUTH_TXN_COOKIE, verifyOAuthTxn } from "@/lib/auth/oauthTxnCookie";
import { checkRateLimit } from "@/lib/auth/rateLimit";
import { createSession, REFRESH_COOKIE, REFRESH_COOKIE_OPTIONS } from "@/lib/auth/session";
import { upsertDiscordUser } from "@/lib/auth/upsertUser";
import { exchangeCodeForToken, fetchDiscordUser } from "@/lib/discord";

const LOGIN_ERROR_PATH = "/login-error";

// design doc §9.1: same bucket/limits reasoning as the login route.
const RATE_LIMIT = { windowMs: 60_000, limit: 10 };

function redirectToError(request: NextRequest): NextResponse {
  // design doc §4 step 8: generic failure response — never reveals which
  // specific check failed, to avoid giving an attacker probing feedback.
  const response = NextResponse.redirect(new URL(LOGIN_ERROR_PATH, request.url));
  response.cookies.delete(OAUTH_TXN_COOKIE);
  return response;
}

// design doc §4, GET /api/auth/discord/callback
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!(await checkRateLimit("oauth-callback", rateLimitClientIp(request), RATE_LIMIT))) {
    return NextResponse.json({ error: "rate_limited" }, { status: 429 });
  }

  const code = request.nextUrl.searchParams.get("code");
  const stateParam = request.nextUrl.searchParams.get("state");
  const txnCookie = request.cookies.get(OAUTH_TXN_COOKIE)?.value;

  if (!code || !stateParam || !txnCookie) {
    return redirectToError(request);
  }

  const txn = await verifyOAuthTxn(txnCookie);
  if (!txn) {
    return redirectToError(request);
  }

  if (!constantTimeEqual(stateParam, txn.state)) {
    return redirectToError(request);
  }

  try {
    const tokenResponse = await exchangeCodeForToken(code, txn.codeVerifier);
    const discordUser = await fetchDiscordUser(tokenResponse.access_token);
    // tokenResponse's access/refresh tokens are used only for the fetch
    // above and discarded here — never persisted (design doc §1, §5).

    const user = await upsertDiscordUser(discordUser);
    const { refreshToken } = await createSession(user.id, {
      userAgent: request.headers.get("user-agent") ?? undefined,
      ipAddress: trustedRemoteAddress(request)
    });

    const response = NextResponse.redirect(new URL("/", request.url));
    response.cookies.set(REFRESH_COOKIE, refreshToken, REFRESH_COOKIE_OPTIONS);
    response.cookies.delete(OAUTH_TXN_COOKIE);

    // docs/settings-appearance-design.md §3.6: pull-on-login, done here
    // rather than on every page load — this is the one server-side moment
    // that already has a fresh row and an outgoing response to attach
    // cookies to. theme_sync always gets set to the account's real value
    // (correcting any local drift, e.g. sync turned off on a different
    // device); theme itself is only overwritten when sync is actually on,
    // so a sync-off account never overrides this device's own choice.
    response.cookies.set(THEME_SYNC_COOKIE, user.themeSyncEnabled ? "1" : "0", THEME_COOKIE_OPTIONS);
    if (user.themeSyncEnabled) {
      response.cookies.set(THEME_COOKIE, resolveThemeId(user.theme), THEME_COOKIE_OPTIONS);
    }

    return response;
  } catch {
    return redirectToError(request);
  }
}
