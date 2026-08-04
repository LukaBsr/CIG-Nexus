import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { blockUser, listBlocks } from "@/lib/internal/blocks";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

// docs/social/friends-dms-design.md §2.8 (BLOCK_USER). A nonexistent
// blocked_id surfaces as a foreign-key constraint violation here, same
// convention createInvite/createChannel already use — no separate
// existence pre-check.
export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as { blocker_id?: unknown; blocked_id?: unknown } | null;
  if (!body || typeof body.blocker_id !== "string" || typeof body.blocked_id !== "string") {
    return NextResponse.json({ error: "blocker_id and blocked_id are required" }, { status: 400 });
  }

  try {
    const ok = await blockUser(body.blocker_id, body.blocked_id);
    if (!ok) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    return NextResponse.json({ ok: true });
  } catch (err) {
    if (isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}

// docs/social/friends-dms-design.md §2.8 (LIST_BLOCKS) — also the query
// FriendHandler's IDENTIFY-time blocked_user_ids hydration uses (§3.3),
// since both need the same "who has this user blocked" list.
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const userId = request.nextUrl.searchParams.get("user_id");
  if (!userId) {
    return NextResponse.json({ error: "user_id is required" }, { status: 400 });
  }

  const blocks = await listBlocks(userId);
  if (!blocks) {
    return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
  }
  return NextResponse.json({ blocks });
}
