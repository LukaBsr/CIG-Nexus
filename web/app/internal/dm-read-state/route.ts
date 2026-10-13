import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { markDmRead } from "@/lib/internal/dmReadState";

// docs/social/friends-dms-design.md §3.7 (MARK_DM_READ).
export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as {
    user_id?: unknown;
    peer_id?: unknown;
    up_to_message_id?: unknown;
  } | null;
  if (
    !body ||
    typeof body.user_id !== "string" ||
    typeof body.peer_id !== "string" ||
    typeof body.up_to_message_id !== "number" ||
    !Number.isInteger(body.up_to_message_id)
  ) {
    return NextResponse.json({ error: "user_id, peer_id, and an integer up_to_message_id are required" }, { status: 400 });
  }

  const result = await markDmRead(body.user_id, body.peer_id, body.up_to_message_id);
  if (!result.ok) {
    return NextResponse.json({ error: "invalid user_id, peer_id, or up_to_message_id" }, { status: 400 });
  }
  return NextResponse.json({ marker_seq: result.markerSeq, unread: result.unread });
}
