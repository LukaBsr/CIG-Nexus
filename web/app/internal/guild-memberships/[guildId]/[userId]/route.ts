import { type NextRequest, NextResponse } from "next/server";

import { deleteMembership } from "@/lib/internal/catalog";
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
