import { type NextRequest, NextResponse } from "next/server";

import { createMessage, getMessages, InvalidReferenceError } from "@/lib/internal/messages";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

const DEFAULT_LIMIT = 50;
const MAX_LIMIT = 100;

// docs/social-presence-design.md §4.5: called fire-and-forget, after the
// C++ server has already broadcast the message — this only needs to
// confirm persistence, nothing downstream is waiting on a rich response.
export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as {
    channel_id?: unknown;
    user_id?: unknown;
    content?: unknown;
    seq?: unknown;
  } | null;

  if (
    !body ||
    (body.channel_id !== null && typeof body.channel_id !== "undefined" && typeof body.channel_id !== "string") ||
    typeof body.user_id !== "string" ||
    typeof body.content !== "string" ||
    typeof body.seq !== "number" ||
    !Number.isInteger(body.seq)
  ) {
    return NextResponse.json(
      { error: "user_id, content, and an integer seq are required; channel_id must be a string or null" },
      { status: 400 }
    );
  }

  try {
    const result = await createMessage(
      typeof body.channel_id === "string" ? body.channel_id : null,
      body.user_id,
      body.content,
      body.seq
    );
    return NextResponse.json(result, { status: 201 });
  } catch (err) {
    if (err instanceof InvalidReferenceError || isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}

// §4.4: the first read-through internal API call — every call is a live
// round trip, nothing here is cached in C++.
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const params = request.nextUrl.searchParams;
  const channelId = params.get("channel_id");

  const beforeSeqParam = params.get("before_seq");
  let beforeSeq: number | null = null;
  if (beforeSeqParam !== null) {
    beforeSeq = Number(beforeSeqParam);
    if (!Number.isInteger(beforeSeq)) {
      return NextResponse.json({ error: "before_seq must be an integer" }, { status: 400 });
    }
  }

  const limitParam = params.get("limit");
  let limit = DEFAULT_LIMIT;
  if (limitParam !== null) {
    limit = Number(limitParam);
    if (!Number.isInteger(limit) || limit < 1 || limit > MAX_LIMIT) {
      return NextResponse.json({ error: `limit must be an integer between 1 and ${MAX_LIMIT}` }, { status: 400 });
    }
  }

  try {
    const page = await getMessages(channelId, beforeSeq, limit);
    return NextResponse.json(page);
  } catch (err) {
    if (err instanceof InvalidReferenceError) {
      return NextResponse.json({ error: "invalid channel_id" }, { status: 400 });
    }
    throw err;
  }
}
