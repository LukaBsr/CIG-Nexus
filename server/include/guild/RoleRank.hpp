#ifndef CIG_NEXUS_GUILD_ROLE_RANK_HPP
#define CIG_NEXUS_GUILD_ROLE_RANK_HPP

namespace guild {

// docs/social-presence-design.md §2.2: every predicate compares against one
// of these constants, never a literal — adding a tier later is an additive
// change here, not a rework of every call site. Deliberately no upper bound
// in the schema (guild_memberships.role_rank CHECK (role_rank >= 0)) or
// here — these three are what's reachable today (SET_MEMBER_ROLE only
// accepts < kOwnerRank), not a hard ceiling on the model.
constexpr int kMemberRank = 0;
constexpr int kOfficerRank = 1;
constexpr int kOwnerRank = 2;

} // namespace guild

#endif // CIG_NEXUS_GUILD_ROLE_RANK_HPP
