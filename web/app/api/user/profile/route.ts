import { eq } from "drizzle-orm";
import { type NextRequest, NextResponse } from "next/server";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { findActiveSessionByRefreshToken, REFRESH_COOKIE } from "@/lib/auth/session";
import {
  isValidAccentColor,
  isValidBio,
  isValidDisplayName,
  isValidStatusMessage,
  resolveAvatarUrl,
  resolveDisplayName
} from "@/lib/user/profile";

// docs/social/friends-dms-design.md §4.4. Session-cookie authenticated,
// same shape as PATCH /api/user/appearance — this lives under
// web/app/api/*, not web/app/internal/*, since a browser calls it
// directly and profile data never needs to flow over the C++/WebSocket
// protocol to be written (§4.5). The target user is resolved exclusively
// from the caller's own session; there is no user_id field in the request
// body for a caller to substitute another account's id into.
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
    display_name?: unknown;
    bio?: unknown;
    status_message?: unknown;
    accent_color?: unknown;
  } | null;
  if (!body) {
    return NextResponse.json({ error: "invalid request body" }, { status: 400 });
  }

  const update: {
    displayName?: string | null;
    bio?: string | null;
    statusMessage?: string | null;
    accentColor?: string | null;
  } = {};

  if (body.display_name !== undefined) {
    if (body.display_name !== null && (typeof body.display_name !== "string" || !isValidDisplayName(body.display_name))) {
      return NextResponse.json({ error: "display_name must be 1-32 characters, or null" }, { status: 400 });
    }
    update.displayName = body.display_name;
  }

  if (body.bio !== undefined) {
    if (body.bio !== null && (typeof body.bio !== "string" || !isValidBio(body.bio))) {
      return NextResponse.json({ error: "bio must be at most 300 characters, or null" }, { status: 400 });
    }
    update.bio = body.bio;
  }

  if (body.status_message !== undefined) {
    if (
      body.status_message !== null &&
      (typeof body.status_message !== "string" || !isValidStatusMessage(body.status_message))
    ) {
      return NextResponse.json({ error: "status_message must be at most 100 characters, or null" }, { status: 400 });
    }
    update.statusMessage = body.status_message;
  }

  if (body.accent_color !== undefined) {
    if (body.accent_color !== null && (typeof body.accent_color !== "string" || !isValidAccentColor(body.accent_color))) {
      return NextResponse.json({ error: "accent_color must be a #rrggbb hex string, or null" }, { status: 400 });
    }
    update.accentColor = body.accent_color;
  }

  if (Object.keys(update).length === 0) {
    return NextResponse.json(
      { error: "at least one of display_name, bio, status_message, accent_color is required" },
      { status: 400 }
    );
  }

  const [updated] = await db.update(users).set(update).where(eq(users.id, session.userId)).returning();

  return NextResponse.json({
    display_name: resolveDisplayName(updated),
    bio: updated.bio,
    status_message: updated.statusMessage,
    accent_color: updated.accentColor,
    avatar_url: resolveAvatarUrl(updated)
  });
}
