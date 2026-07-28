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

const app = next({ dev: false });
const handle = app.getRequestHandler();

function isInternalRequest(req) {
  const url = req.url ?? "";
  return url === INTERNAL_PATH_PREFIX || url.startsWith(`${INTERNAL_PATH_PREFIX}/`);
}

function notFound(res) {
  res.statusCode = 404;
  res.setHeader("Content-Type", "application/json");
  res.end(JSON.stringify({ error: "not found" }));
}

app.prepare().then(() => {
  createServer((req, res) => {
    if (isInternalRequest(req)) {
      notFound(res);
      return;
    }
    handle(req, res);
  }).listen(PORT, () => {
    console.log(`Public server listening on port ${PORT}`);
  });

  createServer((req, res) => {
    if (!isInternalRequest(req)) {
      notFound(res);
      return;
    }
    handle(req, res);
  }).listen(INTERNAL_PORT, () => {
    console.log(`Internal server listening on port ${INTERNAL_PORT}`);
  });
});
