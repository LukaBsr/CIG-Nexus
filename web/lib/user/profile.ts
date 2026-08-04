import type { users } from "@/db/schema";

// docs/social/friends-dms-design.md §4.1 — mirrors the CHECK constraints
// on the users table exactly, so a request that would fail the DB
// constraint is rejected here first with a clear message instead of
// surfacing as a generic Postgres error.
export const DISPLAY_NAME_MAX_LENGTH = 32;
export const BIO_MAX_LENGTH = 300;
export const STATUS_MESSAGE_MAX_LENGTH = 100;
export const ACCENT_COLOR_PATTERN = /^#[0-9a-fA-F]{6}$/;

export function isValidDisplayName(value: string): boolean {
  return value.length >= 1 && value.length <= DISPLAY_NAME_MAX_LENGTH;
}

export function isValidBio(value: string): boolean {
  return value.length <= BIO_MAX_LENGTH;
}

export function isValidStatusMessage(value: string): boolean {
  return value.length <= STATUS_MESSAGE_MAX_LENGTH;
}

export function isValidAccentColor(value: string): boolean {
  return ACCENT_COLOR_PATTERN.test(value);
}

type UserRow = typeof users.$inferSelect;

// §4.1's resolution order: display_name -> discord_global_name ->
// discord_username. Never null — discord_username is NOT NULL, so this
// always bottoms out in something renderable.
export function resolveDisplayName(user: Pick<UserRow, "displayName" | "discordGlobalName" | "discordUsername">): string {
  return user.displayName ?? user.discordGlobalName ?? user.discordUsername;
}

// §4.1: custom_avatar_path if set, else the Discord CDN avatar the client
// has never rendered before now (discord_avatar_hash was stored but
// unused) — constructed the same way Discord's own docs specify. Returns
// null (not a placeholder URL) when the user has neither, so callers
// render their own fallback (initials, default icon).
export function resolveAvatarUrl(
  user: Pick<UserRow, "customAvatarPath" | "discordId" | "discordAvatarHash">
): string | null {
  if (user.customAvatarPath) {
    return user.customAvatarPath;
  }
  if (user.discordAvatarHash) {
    const ext = user.discordAvatarHash.startsWith("a_") ? "gif" : "png";
    return `https://cdn.discordapp.com/avatars/${user.discordId}/${user.discordAvatarHash}.${ext}`;
  }
  return null;
}

export interface WireProfile {
  user_id: string;
  display_name: string;
  bio: string | null;
  status_message: string | null;
  accent_color: string | null;
  avatar_url: string | null;
}
