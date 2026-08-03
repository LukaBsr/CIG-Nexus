import { type NextRequest, NextResponse } from "next/server";

import { getLastSequence } from "@/lib/internal/messages";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

// docs/guilds/social-presence-design.md §4.3: fetched once at C++ server startup,
// alongside catalog hydration, to seed ChatHandler's/ChannelHandler's
// message_id counters from the durable high-water mark instead of always
// starting at 0.
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const lastSequence = await getLastSequence();
  return NextResponse.json(lastSequence);
}
