import { type NextRequest, NextResponse } from "next/server";

import { rejectJoinRequest } from "@/lib/internal/joinRequests";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

// docs/social-presence-design.md §1.9 (REJECT_JOIN_REQUEST). Keyed by
// (guildId, userId) rather than the join request's own row id — the
// protocol message itself (REJECT_JOIN_REQUEST { guild_id, user_id }) never
// carries a request id, and LIST_JOIN_REQUESTS's response doesn't expose
// one either, so C++ has no id to reference. Mirrors the composite-key path
// DELETE /internal/guild-memberships/:guildId/:userId already uses for the
// same reason.
export async function DELETE(
  request: NextRequest,
  { params }: { params: Promise<{ guildId: string; userId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { guildId, userId } = await params;
  const rejected = await rejectJoinRequest(guildId, userId);
  if (!rejected) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ ok: true });
}
