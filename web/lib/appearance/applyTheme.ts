import { THEME_COOKIE, writeClientCookie } from "./cookie";
import { resolveThemeId } from "./themes";

// docs/settings-appearance-design.md §3.1: applies immediately and locally,
// unconditionally — the data-theme attribute (§2.1) plus the cookie that
// makes it durable across reloads (§3.2). Never makes a network call itself;
// the account-sync push (§3.5) is a separate, additive step the Appearance
// section layers on top of this when sync is on, not a replacement for it.
export function applyTheme(id: string): void {
  const resolved = resolveThemeId(id);
  document.documentElement.setAttribute("data-theme", resolved);
  writeClientCookie(THEME_COOKIE, resolved);
}
