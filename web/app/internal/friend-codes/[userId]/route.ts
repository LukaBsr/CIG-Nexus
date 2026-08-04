import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { fetchFriendCode } from "@/lib/internal/friends";

// docs/social/friends-dms-design.md §1.5/§1.6 (FETCH_FRIEND_CODE).
export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ userId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { userId } = await params;
  const code = await fetchFriendCode(userId);
  if (!code) {
    return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
  }
  return NextResponse.json({ code });
}
