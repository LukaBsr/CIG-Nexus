import { randomBytes } from "node:crypto";

// docs/social/friends-dms-design.md §1.3: the same "opaque random token"
// shape as generateInviteCode() (web/lib/internal/inviteCodes.ts) — 10
// random bytes, base64url, ~80 bits of entropy. Deliberately not shared
// as a common helper with the invite generator — a friend code colliding
// with the invite code's length/alphabet choice is a coincidence of both
// wanting "opaque random token," not a reason to couple their futures.
export function generateFriendCode(): string {
  return randomBytes(10).toString("base64url");
}
