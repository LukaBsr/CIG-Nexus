// docs/design/brand-guidelines.md §2.5 (item 2): guild icons are a deliberate
// theme-independent exception — the background is hashed from the guild id
// and does not follow the active theme, so the initials must not follow it
// either (a theme-token foreground would go dark-on-dark on a light theme).
// Lightness is 30%, not 32%: at 32% the worst-case hue gives 4.2:1 against
// the initials color; at 30% it gives 4.6:1 (brand-guidelines §2.6, item 5).
export const GUILD_ICON_FOREGROUND = "#edeffb";

export function hashHue(id: string): number {
  let hash = 0;
  for (let i = 0; i < id.length; i++) {
    hash = (hash * 31 + id.charCodeAt(i)) | 0;
  }
  return Math.abs(hash) % 360;
}

export function guildIconBackground(hue: number): string {
  return `hsl(${hue}, 45%, 30%)`;
}
