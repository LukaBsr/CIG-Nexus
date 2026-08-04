import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { unblockUser } from "@/lib/internal/blocks";

// docs/social/friends-dms-design.md §2.8 (UNBLOCK_USER).
export async function DELETE(
  request: NextRequest,
  { params }: { params: Promise<{ blockerId: string; blockedId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { blockerId, blockedId } = await params;
  const removed = await unblockUser(blockerId, blockedId);
  if (!removed) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ ok: true });
}
