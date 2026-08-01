import { randomBytes } from "node:crypto";

// docs/social-presence-design.md §1.2: 10 random bytes ≈ 80 bits of
// entropy — comfortably impractical to brute-force, matching the same
// "opaque random value" pattern web/lib/auth/session.ts already uses for
// the refresh cookie. Not derived from guild_id or created_at.
export function generateInviteCode(): string {
  return randomBytes(10).toString("base64url");
}
