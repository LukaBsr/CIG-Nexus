# Frozen web-signer JWT fixture

`web-signer-token.txt` is one access JWT produced by the **real** web signer
(`issueAccessJwt` in `web/lib/auth/jwt.ts`), and `public.pem` is the matching
RS256 public key. `server/tests/auth/JwtVerifier.contract.test.cpp` verifies
the token with `auth::JwtVerifier` using an injected clock, so the C++
verifier is exercised against real signer output rather than a hand-built
token. `web/lib/auth/jwt.test.ts` checks the fixture still matches what the
signer emits today (header, claim set, `iss`, `aud`, TTL), so signer drift
fails a web test until the fixture is regenerated.

Claims: `sub` `u_00000000-0000-4000-8000-000000000001`, `discord_id`
`123456789012345678`, `username` `fixture_user`, `sid`
`00000000-0000-4000-8000-0000000000aa`, `iat` `1790000000`, `exp`
`1790000900` (`iat` + 15 min), `iss` `cig-nexus-web`, `aud`
`cig-nexus-server`. The token's clock is frozen: the C++ test verifies at
`iat + 60`, not at the current time, so the token never "expires" in CI.

## How it was generated

No private key is committed. A throwaway keypair was generated, one token was
signed with the real signer, and the private key was then **shredded**:

1. `openssl genrsa` + `openssl pkcs8 -topk8 -nocrypt` for `private.pem` (mode
   600), `openssl rsa -pubout` for `public.pem`, in a temp directory.
2. A throwaway script set `Date.now = () => 1_790_000_000_000`, pointed
   `SESSION_JWT_PRIVATE_KEY_PATH` at the temp `private.pem`, dynamically
   imported `web/lib/auth/jwt.ts`, and called `issueAccessJwt` with the
   claims above (run with `npx tsx` from `web/`).
3. The token was written to `web-signer-token.txt`, `public.pem` was copied
   next to it, and `private.pem` was `shred -u`'d with the temp directory.

To regenerate (e.g. after the signer's claims change), repeat those steps
with a **new** throwaway keypair — the old private key no longer exists —
replace both files, and update the claims above if you changed them.
