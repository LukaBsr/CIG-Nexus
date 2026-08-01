// docs/social-presence-design.md §2.2: named rank constants, mirrored from
// server/include/guild/RoleRank.hpp. Kept in sync manually — there is no
// shared-schema codegen between the two languages for this value.
export const kMemberRank = 0;
export const kOfficerRank = 1;
export const kOwnerRank = 2;

// Static in-code registry (§2.2's ARBITRATION, recommended over a
// role_themes table while exactly one theme exists) mapping a guild's
// role_theme -> role_rank -> display label. Never consulted by any
// permission predicate — resolution is purely cosmetic.
const ROLE_THEMES: Record<string, Record<number, string>> = {
  pirate: {
    [kMemberRank]: "Crew",
    [kOfficerRank]: "Officer",
    [kOwnerRank]: "Captain"
  }
};

// Falls back to a generic label for a role_rank with no entry in the
// guild's theme (e.g. a theme not yet updated to cover a newly-added tier)
// — a display gap should degrade, not break the response.
export function resolveRoleLabel(theme: string, roleRank: number): string {
  return ROLE_THEMES[theme]?.[roleRank] ?? `Rank ${roleRank}`;
}
