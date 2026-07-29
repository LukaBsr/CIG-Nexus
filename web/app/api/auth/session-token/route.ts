import { eq } from "drizzle-orm";
import { type NextRequest, NextResponse } from "next/server";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { issueAccessJwt } from "@/lib/auth/jwt";
import { findActiveSessionByRefreshToken, REFRESH_COOKIE } from "@/lib/auth/session";

// design doc §6, "Bridging the browser-held token to the WebSocket":
// authenticated by the httpOnly refresh cookie, returns a short-lived
// access JWT for the web client to embed in IDENTIFY.
export async function GET(request: NextRequest): Promise<NextResponse> {
  const refreshToken = request.cookies.get(REFRESH_COOKIE)?.value;
  if (!refreshToken) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  const session = await findActiveSessionByRefreshToken(refreshToken);
  if (!session) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  const [user] = await db.select().from(users).where(eq(users.id, session.userId));
  if (!user) {
    return NextResponse.json({ error: "not authenticated" }, { status: 401 });
  }

  const { token, expiresAt } = await issueAccessJwt({
    sub: `u_${user.id}`,
    discordId: user.discordId,
    username: user.discordUsername,
    sid: session.id
  });

  return NextResponse.json({ token, expires_at: expiresAt.toISOString() });
}
