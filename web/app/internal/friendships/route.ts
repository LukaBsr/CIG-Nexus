import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { listFriends } from "@/lib/internal/friends";

// docs/social/friends-dms-design.md §1.5/§1.6 (LIST_FRIENDS).
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const userId = request.nextUrl.searchParams.get("user_id");
  if (!userId) {
    return NextResponse.json({ error: "user_id is required" }, { status: 400 });
  }

  const friends = await listFriends(userId);
  if (!friends) {
    return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
  }
  return NextResponse.json({ friends });
}
