// docs/settings/appearance-design.md §3.2. Non-httpOnly on purpose — client
// JS must read/write it directly for an instant local theme switch that
// doesn't wait on a request round trip (contrast with the httpOnly
// REFRESH_COOKIE in web/lib/auth/session.ts, which must NOT be
// client-readable). This is the only local source of truth for the
// selected theme; nothing here talks to the account/sync system (§3.5).
export const THEME_COOKIE = "theme";

// docs/settings/appearance-design.md §3.3: mirrors the account's
// theme_sync_enabled column locally ("1"/"0") purely so the Appearance
// section can render the toggle's correct state on open without a GET
// round trip (§3.5 deliberately has no GET endpoint) — kept in sync
// wherever THEME_COOKIE is, by the same writes.
export const THEME_SYNC_COOKIE = "theme_sync";

// ~13 months — comfortably under Chrome's 400-day Max-Age cap, long enough
// that a returning user's local preference doesn't silently reset.
export const THEME_COOKIE_MAX_AGE_SECONDS = 60 * 60 * 24 * 400;

// For server-side response cookies (Route Handlers / Server Components).
// Mirrors web/lib/auth/session.ts's REFRESH_COOKIE_OPTIONS shape, with
// httpOnly flipped (see above). Shared by both THEME_COOKIE and
// THEME_SYNC_COOKIE — only the name/value differ between them.
export const THEME_COOKIE_OPTIONS = {
  httpOnly: false,
  secure: process.env.NODE_ENV === "production",
  sameSite: "lax" as const,
  path: "/",
  maxAge: THEME_COOKIE_MAX_AGE_SECONDS
};

// Client-side read/write — safe to import from client components only
// (document isn't defined during SSR).
export function readClientCookie(name: string): string | null {
  if (typeof document === "undefined") {
    return null;
  }
  const match = document.cookie.match(new RegExp(`(?:^|; )${name}=([^;]*)`));
  return match ? decodeURIComponent(match[1]) : null;
}

export function writeClientCookie(name: string, value: string): void {
  const secure = process.env.NODE_ENV === "production" ? "; secure" : "";
  document.cookie = `${name}=${encodeURIComponent(value)}; path=/; max-age=${THEME_COOKIE_MAX_AGE_SECONDS}; samesite=lax${secure}`;
}
