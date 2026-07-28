import { type NextRequest, NextResponse } from "next/server";

import { getRevokedSessionIds } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

// design doc §9: backs the C++ server's poll-based revocation cache.
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const sinceParam = request.nextUrl.searchParams.get("since");
  const since = sinceParam ? new Date(sinceParam) : new Date(0);
  if (Number.isNaN(since.getTime())) {
    return NextResponse.json({ error: "invalid since" }, { status: 400 });
  }

  const revokedSessionIds = await getRevokedSessionIds(since);
  return NextResponse.json({ revoked_session_ids: revokedSessionIds, as_of: new Date().toISOString() });
}
