import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { listDmConversations } from "@/lib/internal/dmConversations";

// docs/social/friends-dms-design.md §3.5/§3.6 (LIST_DM_CONVERSATIONS).
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const userId = request.nextUrl.searchParams.get("user_id");
  if (!userId) {
    return NextResponse.json({ error: "user_id is required" }, { status: 400 });
  }

  const conversations = await listDmConversations(userId);
  if (!conversations) {
    return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
  }
  return NextResponse.json({ conversations });
}
