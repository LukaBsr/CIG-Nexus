import { THEME_COOKIE, THEME_COOKIE_MAX_AGE_SECONDS } from "./cookie";
import { resolveThemeId } from "./themes";

// docs/settings-appearance-design.md §3.1: applies immediately and locally,
// unconditionally — the data-theme attribute (§2.1) plus the cookie that
// makes it durable across reloads (§3.2). Never makes a network call; the
// account-sync push (§3.5, not built yet) is a separate, additive step
// layered on top of this, not a replacement for it.
export function applyTheme(id: string): void {
  const resolved = resolveThemeId(id);
  document.documentElement.setAttribute("data-theme", resolved);
  const secure = process.env.NODE_ENV === "production" ? "; secure" : "";
  document.cookie = `${THEME_COOKIE}=${resolved}; path=/; max-age=${THEME_COOKIE_MAX_AGE_SECONDS}; samesite=lax${secure}`;
}
