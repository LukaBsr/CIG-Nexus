// docs/settings-appearance-design.md §3.2. Non-httpOnly on purpose — client
// JS must read/write it directly for an instant local theme switch that
// doesn't wait on a request round trip (contrast with the httpOnly
// REFRESH_COOKIE in web/lib/auth/session.ts, which must NOT be
// client-readable). This is the only local source of truth for the
// selected theme; nothing here talks to the account/sync system (§3.5).
export const THEME_COOKIE = "theme";

// ~13 months — comfortably under Chrome's 400-day Max-Age cap, long enough
// that a returning user's local preference doesn't silently reset.
export const THEME_COOKIE_MAX_AGE_SECONDS = 60 * 60 * 24 * 400;

// For server-side response cookies (Route Handlers / Server Components).
// Mirrors web/lib/auth/session.ts's REFRESH_COOKIE_OPTIONS shape, with
// httpOnly flipped (see above).
export const THEME_COOKIE_OPTIONS = {
  httpOnly: false,
  secure: process.env.NODE_ENV === "production",
  sameSite: "lax" as const,
  path: "/",
  maxAge: THEME_COOKIE_MAX_AGE_SECONDS
};
