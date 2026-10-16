import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { fetchUnreadCounts } from "@/lib/internal/dmReadState";

// docs/social/friends-dms-design.md §3.7 (FETCH_UNREAD_COUNTS). One call
// for every one of the caller's DM conversations, not one per
// conversation — fetchUnreadCounts itself is the single SQL statement
// that makes that true.
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const userId = request.nextUrl.searchParams.get("user_id");
  if (!userId) {
    return NextResponse.json({ error: "user_id is required" }, { status: 400 });
  }

  const counts = await fetchUnreadCounts(userId);
  if (!counts) {
    return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
  }
  return NextResponse.json({ counts });
}
