import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { acceptFriendRequest } from "@/lib/internal/friends";

// docs/social/friends-dms-design.md §1.4/§1.6 (ACCEPT_FRIEND_REQUEST).
// recipientId (the caller accepting) is in the path alongside requesterId
// — mirrors the composite-key path DELETE
// /internal/guild-join-requests/:guildId/:userId already uses for the
// same "no single row id the protocol layer carries" reason.
export async function POST(
  request: NextRequest,
  { params }: { params: Promise<{ requesterId: string; recipientId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { requesterId, recipientId } = await params;
  const result = await acceptFriendRequest(recipientId, requesterId);
  if (!result.ok) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json(result);
}
