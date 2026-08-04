import { eq } from "drizzle-orm";
import { type NextRequest, NextResponse } from "next/server";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { findActiveSessionByRefreshToken, REFRESH_COOKIE } from "@/lib/auth/session";
import { deleteAvatar, InvalidAvatarError, saveAvatar } from "@/lib/user/avatarStorage";
import { resolveAvatarUrl } from "@/lib/user/profile";

async function requireSession(request: NextRequest) {
  const refreshToken = request.cookies.get(REFRESH_COOKIE)?.value;
  if (!refreshToken) {
    return null;
  }
  return findActiveSessionByRefreshToken(refreshToken);
}

// docs/social/friends-dms-design.md §4.4/§5. The first file-upload route
// in this codebase — multipart/form-data, one file field. Validation
// beyond session auth: size cap and magic-byte type sniffing both happen
// inside saveAvatar (web/lib/user/avatarStorage.ts), not here, so there's
// one place those rules live.
export async function POST(request: NextRequest): Promise<NextResponse> {
  const session = await requireSession(request);
  if (!session) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  const formData = await request.formData().catch(() => null);
  const file = formData?.get("avatar");
  if (!file || typeof file === "string") {
    return NextResponse.json({ error: "avatar file is required" }, { status: 400 });
  }

  const bytes = Buffer.from(await file.arrayBuffer());

  let newAvatarPath: string;
  try {
    newAvatarPath = await saveAvatar(bytes);
  } catch (err) {
    if (err instanceof InvalidAvatarError) {
      return NextResponse.json({ error: err.message }, { status: 400 });
    }
    throw err;
  }

  const [previous] = await db.select({ customAvatarPath: users.customAvatarPath }).from(users).where(eq(users.id, session.userId));

  const [updated] = await db
    .update(users)
    .set({ customAvatarPath: newAvatarPath })
    .where(eq(users.id, session.userId))
    .returning();

  // Best-effort cleanup of the file the new upload replaces — after the
  // DB write commits, so a delete failure here never leaves the account
  // pointing at a file that no longer exists.
  if (previous?.customAvatarPath) {
    void deleteAvatar(previous.customAvatarPath);
  }

  return NextResponse.json({ avatar_url: resolveAvatarUrl(updated) });
}

// Reverts to the Discord avatar (§4.4) — clears custom_avatar_path and
// deletes the now-orphaned file from disk.
export async function DELETE(request: NextRequest): Promise<NextResponse> {
  const session = await requireSession(request);
  if (!session) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  const [previous] = await db.select({ customAvatarPath: users.customAvatarPath }).from(users).where(eq(users.id, session.userId));

  const [updated] = await db
    .update(users)
    .set({ customAvatarPath: null })
    .where(eq(users.id, session.userId))
    .returning();

  if (previous?.customAvatarPath) {
    void deleteAvatar(previous.customAvatarPath);
  }

  return NextResponse.json({ avatar_url: resolveAvatarUrl(updated) });
}
