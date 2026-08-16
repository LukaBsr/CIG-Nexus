import { type NextRequest, NextResponse } from "next/server";

import { getUserProfileSummary } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

// docs/social/friends-dms-design.md §4.5, revised at implementation: backs
// IdentifyHandler's Session.display_name/avatar_url hydration (mirrors the
// fetchBlocks/fetchFriends calls already made there) — a single-user
// lookup, not a search endpoint (no username/query lookup is exposed).
export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ id: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { id } = await params;
  const profile = await getUserProfileSummary(id);
  if (!profile) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json(profile);
}
