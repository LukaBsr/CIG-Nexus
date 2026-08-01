import { type NextRequest, NextResponse } from "next/server";

import { createGuild, InvalidReferenceError, type GuildVisibility } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

const VALID_VISIBILITIES: GuildVisibility[] = ["open", "application", "private"];

export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as
    | { name?: unknown; owner_id?: unknown; visibility?: unknown }
    | null;
  if (!body || typeof body.name !== "string" || typeof body.owner_id !== "string") {
    return NextResponse.json({ error: "name and owner_id are required" }, { status: 400 });
  }
  if (body.visibility !== undefined && !VALID_VISIBILITIES.includes(body.visibility as GuildVisibility)) {
    return NextResponse.json({ error: "visibility must be open, application, or private" }, { status: 400 });
  }

  try {
    const guild = await createGuild(body.name, body.owner_id, body.visibility as GuildVisibility | undefined);
    return NextResponse.json(guild, { status: 201 });
  } catch (err) {
    if (err instanceof InvalidReferenceError || isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}
