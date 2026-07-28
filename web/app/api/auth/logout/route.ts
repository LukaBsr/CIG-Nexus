import { type NextRequest, NextResponse } from "next/server";

import { findActiveSessionByRefreshToken, REFRESH_COOKIE, revokeSession } from "@/lib/auth/session";

// design doc §9.1 checklist: logout revokes the session row immediately.
export async function POST(request: NextRequest): Promise<NextResponse> {
  const refreshToken = request.cookies.get(REFRESH_COOKIE)?.value;

  if (refreshToken) {
    const session = await findActiveSessionByRefreshToken(refreshToken);
    if (session) {
      await revokeSession(session.id);
    }
  }

  const response = NextResponse.json({ ok: true });
  response.cookies.delete(REFRESH_COOKIE);
  return response;
}
