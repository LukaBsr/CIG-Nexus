import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { InvalidReferenceError } from "@/lib/internal/catalog";
import { addFriendByCode, listFriendRequests, sendFriendRequest } from "@/lib/internal/friends";

// docs/social/friends-dms-design.md §1.6. Body carries either
// {requester_id, recipient_id} (SEND_FRIEND_REQUEST) or {requester_id,
// code} (ADD_FRIEND_BY_CODE) — mutually exclusive, both resolve to the
// same underlying transaction (§1.3). Always 200 with a discriminated
// body, same "domain-level result, not a REST resource state" convention
// POST /internal/guild-invites/:code/redeem already uses.
export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as {
    requester_id?: unknown;
    recipient_id?: unknown;
    code?: unknown;
  } | null;
  if (!body || typeof body.requester_id !== "string") {
    return NextResponse.json({ error: "requester_id is required" }, { status: 400 });
  }
  if (typeof body.recipient_id !== "string" && typeof body.code !== "string") {
    return NextResponse.json({ error: "recipient_id or code is required" }, { status: 400 });
  }

  try {
    const result =
      typeof body.recipient_id === "string"
        ? await sendFriendRequest(body.requester_id, body.recipient_id)
        : await addFriendByCode(body.requester_id, body.code as string);
    return NextResponse.json(result);
  } catch (err) {
    if (err instanceof InvalidReferenceError) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}

// docs/social/friends-dms-design.md §1.5 (LIST_FRIEND_REQUESTS).
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const userId = request.nextUrl.searchParams.get("user_id");
  if (!userId) {
    return NextResponse.json({ error: "user_id is required" }, { status: 400 });
  }

  const result = await listFriendRequests(userId);
  if (!result) {
    return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
  }
  return NextResponse.json(result);
}
