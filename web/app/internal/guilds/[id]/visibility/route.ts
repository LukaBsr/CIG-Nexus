import { type NextRequest, NextResponse } from "next/server";

import { setGuildVisibility, type GuildVisibility } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

const VALID_VISIBILITIES: GuildVisibility[] = ["open", "application", "private"];

// docs/social-presence-design.md §1.10 (SET_GUILD_VISIBILITY).
export async function POST(
  request: NextRequest,
  { params }: { params: Promise<{ id: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as { visibility?: unknown } | null;
  if (!body || !VALID_VISIBILITIES.includes(body.visibility as GuildVisibility)) {
    return NextResponse.json({ error: "visibility must be open, application, or private" }, { status: 400 });
  }

  const { id } = await params;
  const updated = await setGuildVisibility(id, body.visibility as GuildVisibility);
  if (!updated) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json(updated);
}
