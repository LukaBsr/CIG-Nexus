import { eq } from "drizzle-orm";
import { type NextRequest, NextResponse } from "next/server";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { THEMES } from "@/lib/appearance/themes";
import { findActiveSessionByRefreshToken, REFRESH_COOKIE } from "@/lib/auth/session";

// docs/settings/appearance-design.md §3.5. Session-cookie authenticated
// (same __session refresh cookie as GET /api/auth/session-token) — this
// lives under web/app/api/*, not web/app/internal/*, since a browser calls
// it directly and the C++/WebSocket protocol has no involvement in
// appearance settings at all. The target user is resolved exclusively from
// the caller's own session; there is no user_id field in the request body
// for a caller to substitute another account's id into.
export async function PATCH(request: NextRequest): Promise<NextResponse> {
  const refreshToken = request.cookies.get(REFRESH_COOKIE)?.value;
  if (!refreshToken) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  const session = await findActiveSessionByRefreshToken(refreshToken);
  if (!session) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as {
    theme?: unknown;
    sync_enabled?: unknown;
  } | null;
  if (!body) {
    return NextResponse.json({ error: "invalid request body" }, { status: 400 });
  }

  const update: { theme?: string; themeSyncEnabled?: boolean } = {};

  if (body.theme !== undefined) {
    if (typeof body.theme !== "string" || !THEMES.some((theme) => theme.id === body.theme)) {
      return NextResponse.json(
        { error: `theme must be one of: ${THEMES.map((theme) => theme.id).join(", ")}` },
        { status: 400 }
      );
    }
    update.theme = body.theme;
  }

  if (body.sync_enabled !== undefined) {
    if (typeof body.sync_enabled !== "boolean") {
      return NextResponse.json({ error: "sync_enabled must be a boolean" }, { status: 400 });
    }
    update.themeSyncEnabled = body.sync_enabled;
  }

  if (Object.keys(update).length === 0) {
    return NextResponse.json({ error: "theme and/or sync_enabled required" }, { status: 400 });
  }

  const [updated] = await db
    .update(users)
    .set(update)
    .where(eq(users.id, session.userId))
    .returning();

  return NextResponse.json({ theme: updated.theme, sync_enabled: updated.themeSyncEnabled });
}
