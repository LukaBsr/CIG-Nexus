import { type NextRequest, NextResponse } from "next/server";

import { redeemInvite } from "@/lib/internal/invites";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { InvalidReferenceError } from "@/lib/internal/catalog";

// docs/social-presence-design.md §1.5: POST /internal/guild-invites/:code/redeem
// — one atomic call. Always 200 with a discriminated JSON body when the
// call itself completes (ok: true/false plus kind/error) — this is a
// domain-level result with five distinct outcomes, not a REST resource
// state, so the C++ caller discriminates on the body rather than the
// status code (which stays reserved for auth/malformed-request failures).
export async function POST(
  request: NextRequest,
  { params }: { params: Promise<{ code: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as { user_id?: unknown } | null;
  if (!body || typeof body.user_id !== "string") {
    return NextResponse.json({ error: "user_id is required" }, { status: 400 });
  }

  const { code } = await params;
  try {
    const result = await redeemInvite(code, body.user_id);
    return NextResponse.json(result);
  } catch (err) {
    if (err instanceof InvalidReferenceError) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}
