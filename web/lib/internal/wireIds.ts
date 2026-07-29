// design doc §5: wire-format ids stay "g_<uuid>" / "c_<uuid>" / "u_<uuid>"
// at the protocol boundary even though the underlying generation scheme is
// now a Postgres UUID rather than a server-assigned counter.
export const toGuildWireId = (id: string): string => `g_${id}`;
export const toChannelWireId = (id: string): string => `c_${id}`;
export const toUserWireId = (id: string): string => `u_${id}`;

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

// Validating the UUID shape here — not just the prefix — matters: an
// id that merely starts with "g_" but isn't a well-formed UUID would
// otherwise reach Postgres as a raw query parameter and fail with a
// generic "invalid input syntax for type uuid" error instead of the
// clean "not found" this should behave as.
function fromWireId(prefix: string, wireId: string): string | null {
  if (!wireId.startsWith(prefix)) {
    return null;
  }
  const id = wireId.slice(prefix.length);
  return UUID_PATTERN.test(id) ? id : null;
}

export const fromGuildWireId = (wireId: string): string | null => fromWireId("g_", wireId);
export const fromChannelWireId = (wireId: string): string | null => fromWireId("c_", wireId);
export const fromUserWireId = (wireId: string): string | null => fromWireId("u_", wireId);
