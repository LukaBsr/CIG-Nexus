import { eq } from "drizzle-orm";
import { type NextRequest, NextResponse } from "next/server";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { checkRateLimitFailOpen } from "@/lib/auth/rateLimit";
import { findActiveSessionByRefreshToken, REFRESH_COOKIE } from "@/lib/auth/session";
import { isBlockedEitherDirection } from "@/lib/internal/blocks";
import { shareAnyGuild } from "@/lib/internal/catalog";
import { areFriends } from "@/lib/internal/friends";
import { fromUserWireId, toUserWireId } from "@/lib/internal/wireIds";
import { resolveAvatarUrl, resolveDisplayName } from "@/lib/user/profile";

// docs/social/friends-dms-design.md §4.6 (v0.8) — generous, since real
// "view profile" usage is occasional clicks, not a hot path; sized to
// blunt scripted probing of the 404-uniformity below, not real traffic.
const RATE_LIMIT = { windowMs: 60_000, limit: 60 };

// docs/social/friends-dms-design.md §4.4/§4.6. Session-cookie
// authenticated — needs the caller's own identity, not just the target's,
// because visibility (§4.6) depends on the caller's relationship to the
// target: canViewProfile(caller, target) := (areFriends || shareAnyGuild)
// && !isBlockedEitherDirection, the exact shape canSendDm (§3.2) uses.
// A nonexistent target, a target with no qualifying relationship, and a
// target blocked either direction all return the identical 404 (§4.6's
// silent-failure extension) — never a distinguishing status/body.
export async function GET(request: NextRequest, { params }: { params: Promise<{ id: string }> }): Promise<NextResponse> {
  const refreshToken = request.cookies.get(REFRESH_COOKIE)?.value;
  if (!refreshToken) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }
  const session = await findActiveSessionByRefreshToken(refreshToken);
  if (!session) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  if (!(await checkRateLimitFailOpen("user-profile-view", session.userId, RATE_LIMIT))) {
    return NextResponse.json({ error: "rate_limited" }, { status: 429 });
  }

  const { id } = await params;
  const targetId = fromUserWireId(id);
  if (!targetId) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }

  const [target] = await db.select().from(users).where(eq(users.id, targetId));
  if (!target) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }

  const [friends, sharedGuild, blocked] = await Promise.all([
    areFriends(session.userId, targetId),
    shareAnyGuild(session.userId, targetId),
    isBlockedEitherDirection(session.userId, targetId)
  ]);
  if ((!friends && !sharedGuild) || blocked) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }

  return NextResponse.json({
    user_id: toUserWireId(target.id),
    display_name: resolveDisplayName(target),
    bio: target.bio,
    status_message: target.statusMessage,
    accent_color: target.accentColor,
    avatar_url: resolveAvatarUrl(target)
  });
}
