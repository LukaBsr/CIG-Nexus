import { type NextRequest, NextResponse } from "next/server";

import { revokeInvite } from "@/lib/internal/invites";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

// docs/social-presence-design.md §1.5: DELETE /internal/guild-invites/:code
// — revoke. guild_id is a query param (not in the path, matching the
// doc's literal path shape) so revocation can be scoped to "this code
// belongs to this guild" (§1.4's REVOKE_INVITE validation) without a
// second lookup.
export async function DELETE(
  request: NextRequest,
  { params }: { params: Promise<{ code: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const guildId = request.nextUrl.searchParams.get("guild_id");
  if (!guildId) {
    return NextResponse.json({ error: "guild_id is required" }, { status: 400 });
  }

  const { code } = await params;
  const revoked = await revokeInvite(guildId, code);
  if (!revoked) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ ok: true });
}
