import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { regenerateFriendCode } from "@/lib/internal/friends";

// docs/social/friends-dms-design.md §1.3/§1.6 (REGENERATE_FRIEND_CODE).
export async function POST(
  request: NextRequest,
  { params }: { params: Promise<{ userId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { userId } = await params;
  const code = await regenerateFriendCode(userId);
  if (!code) {
    return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
  }
  return NextResponse.json({ code });
}
