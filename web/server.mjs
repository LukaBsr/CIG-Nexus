// Custom server for the /internal/* isolation requirement (design doc
// §8.1). `next start` can only bind one port, but the internal catalog API
// (app/internal/*) must be reachable ONLY on a second, unpublished port
// — docker-compose.yml never maps INTERNAL_PORT to the host, so it's only
// reachable from other containers on the compose network.
//
// Both HTTP servers below share the same Next.js request handler, but each
// one blanket-rejects the *other* server's routes before ever calling into
// it. This means the public port structurally cannot reach /internal/*
// regardless of reverse-proxy/ingress config — a second, independent layer
// of defense beyond the port simply not being published, and beyond the
// shared-secret header /internal/* itself checks (design doc §9.1: the
// shared secret alone is not treated as sufficient).
//
// Only used in production (see package.json's "start" script); local dev
// keeps plain `next dev` for its faster iteration loop — the isolation
// guarantee only needs to hold for the deployed stack.
import { createServer } from "node:http";

import next from "next";

const PORT = Number(process.env.PORT ?? 3000);
const INTERNAL_PORT = Number(process.env.INTERNAL_PORT ?? 3001);
const INTERNAL_PATH_PREFIX = "/internal";

// docs/security-audit.md §2.1: X-Forwarded-For is client-controlled with no
// reverse proxy in front of this server, so it can't be trusted for rate
// limiting or for the IP address stored on a session. This server has raw
// socket access (plain node:http), so it is instead the trust boundary
// itself: it stamps every request with the real TCP peer address under a
// header name a route handler can rely on, unconditionally overwriting
// whatever the client sent — never merged, never left in place from the
// incoming request. lib/auth/clientIp.ts reads this header and this header
// alone.
const TRUSTED_REMOTE_ADDR_HEADER = "x-cig-nexus-remote-addr";

const app = next({ dev: false });
const handle = app.getRequestHandler();

function isInternalRequest(req) {
  const url = req.url ?? "";
  return url === INTERNAL_PATH_PREFIX || url.startsWith(`${INTERNAL_PATH_PREFIX}/`);
}

// Node reports IPv4-mapped connections as "::ffff:1.2.3.4" — stripped so
// the stored/rate-limited value is a plain IPv4 address, matching what
// X-Forwarded-For would have contained.
function normalizeRemoteAddress(address) {
  if (!address) {
    return undefined;
  }
  return address.startsWith("::ffff:") ? address.slice("::ffff:".length) : address;
}

function stampTrustedRemoteAddr(req) {
  const remoteAddress = normalizeRemoteAddress(req.socket.remoteAddress);
  if (remoteAddress) {
    req.headers[TRUSTED_REMOTE_ADDR_HEADER] = remoteAddress;
  } else {
    delete req.headers[TRUSTED_REMOTE_ADDR_HEADER];
  }
}

function notFound(res) {
  res.statusCode = 404;
  res.setHeader("Content-Type", "application/json");
  res.end(JSON.stringify({ error: "not found" }));
}

app
  .prepare()
  .then(() => {
    createServer((req, res) => {
      stampTrustedRemoteAddr(req);
      if (isInternalRequest(req)) {
        notFound(res);
        return;
      }
      handle(req, res);
    }).listen(PORT, () => {
      console.log(`Public server listening on port ${PORT}`);
    });

    createServer((req, res) => {
      stampTrustedRemoteAddr(req);
      if (!isInternalRequest(req)) {
        notFound(res);
        return;
      }
      handle(req, res);
    }).listen(INTERNAL_PORT, () => {
      console.log(`Internal server listening on port ${INTERNAL_PORT}`);
    });
  })
  .catch((error) => {
    // Safety net: instrumentation.ts's register() already process.exit()s
    // on a missing required env var (a *thrown* error from register() was
    // verified not to reliably stop Next.js from going on to bind ports
    // anyway), but this still catches any other startup failure that would
    // otherwise leave prepare() silently rejected while nothing is
    // actually listening.
    console.error("Failed to start server:", error);
    process.exit(1);
  });
