import { type NextRequest, NextResponse } from "next/server";

import { deleteMembership, setMemberRole } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

export async function DELETE(
  request: NextRequest,
  { params }: { params: Promise<{ guildId: string; userId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { guildId, userId } = await params;
  const deleted = await deleteMembership(guildId, userId);
  if (!deleted) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ ok: true });
}

// docs/social-presence-design.md §2.4 (SET_MEMBER_ROLE).
export async function PATCH(
  request: NextRequest,
  { params }: { params: Promise<{ guildId: string; userId: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as { role_rank?: unknown } | null;
  if (!body || !Number.isInteger(body.role_rank) || (body.role_rank as number) < 0) {
    return NextResponse.json({ error: "role_rank must be a non-negative integer" }, { status: 400 });
  }

  const { guildId, userId } = await params;
  const updated = await setMemberRole(guildId, userId, body.role_rank as number);
  if (!updated) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json(updated);
}
