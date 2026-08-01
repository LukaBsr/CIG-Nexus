import { type NextRequest, NextResponse } from "next/server";

import { approveJoinRequest } from "@/lib/internal/joinRequests";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

// docs/social-presence-design.md §1.9 (APPROVE_JOIN_REQUEST). Same
// composite-key reasoning as the DELETE route in the parent directory.
export async function POST(
  request: NextRequest,
  { params }: { params: Promise<{ guildId: string; userId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { guildId, userId } = await params;
  const approved = await approveJoinRequest(guildId, userId);
  if (!approved) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json(approved);
}
