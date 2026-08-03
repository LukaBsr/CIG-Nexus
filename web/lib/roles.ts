// docs/guilds/social-presence-design.md §2.2. Mirrored from
// server/include/guild/RoleRank.hpp and web/lib/internal/roleThemes.ts —
// kept in sync manually, no shared-schema codegen between the two. Not
// importing from lib/internal/ here on purpose: that tree is server-only
// (internal API route handlers with direct DB access) and isn't meant to be
// pulled into the client bundle.
export const MEMBER_RANK = 0;
export const OFFICER_RANK = 1;
export const OWNER_RANK = 2;
