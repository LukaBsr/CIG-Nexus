import { type NextRequest, NextResponse } from "next/server";

import { createChannel, InvalidReferenceError } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as {
    guild_id?: unknown;
    name?: unknown;
    channel_type?: unknown;
  } | null;
  if (
    !body ||
    typeof body.guild_id !== "string" ||
    typeof body.name !== "string" ||
    (body.channel_type !== "TEXT" && body.channel_type !== "VOICE")
  ) {
    return NextResponse.json(
      { error: "guild_id, name, and channel_type (TEXT|VOICE) are required" },
      { status: 400 }
    );
  }

  try {
    const channel = await createChannel(body.guild_id, body.name, body.channel_type);
    return NextResponse.json(channel, { status: 201 });
  } catch (err) {
    if (err instanceof InvalidReferenceError || isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}
