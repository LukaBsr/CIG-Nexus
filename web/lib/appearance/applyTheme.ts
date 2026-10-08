import { THEME_COOKIE, writeClientCookie } from "./cookie";
import { resolveThemeId, resolveThemeMode } from "./themes";

// docs/settings/appearance-design.md §3.1: applies immediately and locally,
// unconditionally — the data-theme attribute (§2.1) plus the cookie that
// makes it durable across reloads (§3.2). Never makes a network call itself;
// the account-sync push (§3.5) is a separate, additive step the Appearance
// section layers on top of this when sync is on, not a replacement for it.
export function applyTheme(id: string): void {
  const resolved = resolveThemeId(id);
  document.documentElement.setAttribute("data-theme", resolved);
  // data-mode (§5.4): kept in step with data-theme here too, so switching
  // theme client-side never leaves it stale behind the server-rendered
  // value layout.tsx set on first load.
  document.documentElement.setAttribute("data-mode", resolveThemeMode(id));
  writeClientCookie(THEME_COOKIE, resolved);
}
