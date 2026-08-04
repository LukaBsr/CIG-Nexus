import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { deleteFriendRequest } from "@/lib/internal/friends";

// docs/social/friends-dms-design.md §1.4/§1.6 (REJECT_FRIEND_REQUEST /
// CANCEL_FRIEND_REQUEST) — same deletion either way; which of the two
// wire responses the caller sends back is a C++-side distinction based on
// which message came in, not something this route needs to know.
export async function DELETE(
  request: NextRequest,
  { params }: { params: Promise<{ requesterId: string; recipientId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { requesterId, recipientId } = await params;
  const deleted = await deleteFriendRequest(requesterId, recipientId);
  if (!deleted) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ ok: true });
}
