import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { removeFriend } from "@/lib/internal/friends";

// docs/social/friends-dms-design.md §1.4/§1.6 (REMOVE_FRIEND). Order of
// the two path segments doesn't matter — removeFriend canonically orders
// them itself (§1.2) the same way the friendships table does.
export async function DELETE(
  request: NextRequest,
  { params }: { params: Promise<{ userIdA: string; userIdB: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { userIdA, userIdB } = await params;
  const removed = await removeFriend(userIdA, userIdB);
  if (!removed) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ ok: true });
}
