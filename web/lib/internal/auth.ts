import { constantTimeEqual } from "@/lib/auth/constantTimeEqual";
import { authEnv } from "@/lib/auth/env";

// design doc §8.1/§9.1: /internal/* is authenticated by a shared secret,
// not user sessions. This alone is NOT the isolation mechanism (§8.1) — it
// pairs with the internal routes being unreachable from outside the
// Docker-internal network in the first place (§5 phase).
export function isAuthorizedInternalRequest(request: Request): boolean {
  const provided = request.headers.get("x-internal-secret");
  if (!provided) {
    return false;
  }
  return constantTimeEqual(provided, authEnv.internalApiSharedSecret);
}
