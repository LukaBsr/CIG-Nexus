import { eq } from "drizzle-orm";
import { type NextRequest, NextResponse } from "next/server";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { findActiveSessionByRefreshToken, REFRESH_COOKIE } from "@/lib/auth/session";
import { isBlockedBy } from "@/lib/internal/blocks";
import { fromUserWireId, toUserWireId } from "@/lib/internal/wireIds";
import { resolveAvatarUrl, resolveDisplayName } from "@/lib/user/profile";

// docs/social/friends-dms-design.md §4.4. Session-cookie authenticated —
// needs the caller's own identity, not just the target's, because the
// block check (§2.5/§2.6: "has the target blocked the caller") is
// evaluated against the caller, not anonymously. A blocked caller gets
// the identical 404 a nonexistent user_id would (§2.6's silent-failure
// recommendation) — both checked in this one handler, not two branches
// with different status codes.
export async function GET(request: NextRequest, { params }: { params: Promise<{ id: string }> }): Promise<NextResponse> {
  const refreshToken = request.cookies.get(REFRESH_COOKIE)?.value;
  if (!refreshToken) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }
  const session = await findActiveSessionByRefreshToken(refreshToken);
  if (!session) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
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

  if (await isBlockedBy(targetId, session.userId)) {
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
