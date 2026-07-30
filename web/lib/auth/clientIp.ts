import type { NextRequest } from "next/server";

// docs/security-audit.md §2.1: X-Forwarded-For is attacker-controlled in
// this deployment — there is no reverse proxy in front of web (see
// docker-compose.yml, which publishes web's port directly to the host), so
// trusting it let a client spoof its own rate-limit identity and the IP
// address stored on its session row. web/server.mjs sets this header
// itself from the raw TCP socket's remote address on every request it
// forwards to Next.js, unconditionally overwriting any value the client
// sent — so unlike X-Forwarded-For, it cannot be spoofed by anything
// off-box. It's only set by the custom server (production/`next start`);
// `next dev` never runs server.mjs, so both functions below fall back to
// "no identity" in local dev, same as today.
const TRUSTED_REMOTE_ADDR_HEADER = "x-cig-nexus-remote-addr";

// undefined (not a placeholder string) when absent — callers that persist
// this (e.g. sessions.ip_address, a Postgres `inet` column) must be able to
// tell "unknown" apart from a real value.
export function trustedRemoteAddress(request: NextRequest): string | undefined {
  return request.headers.get(TRUSTED_REMOTE_ADDR_HEADER) ?? undefined;
}

export function clientIp(request: NextRequest): string {
  return trustedRemoteAddress(request) ?? "unknown";
}
