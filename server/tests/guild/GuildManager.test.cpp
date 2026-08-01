#include <catch2/catch_test_macros.hpp>

#include "guild/GuildManager.hpp"

#include <algorithm>

TEST_CASE("GuildManager upsertGuild stores a guild under the given (externally-assigned) id") {
    guild::GuildManager manager;

    guild::Guild& g1 = manager.upsertGuild("g_1", "First", "u_1");
    guild::Guild& g2 = manager.upsertGuild("g_2", "Second", "u_2");

    REQUIRE(g1.id == "g_1");
    REQUIRE(g1.name == "First");
    REQUIRE(g1.owner_id == "u_1");
    REQUIRE(g1.visibility == guild::GuildVisibility::OPEN); // default
    REQUIRE(g1.created_at > 0);

    REQUIRE(g2.id == "g_2");
    REQUIRE(manager.hasGuild("g_1"));
    REQUIRE(manager.hasGuild("g_2"));
    REQUIRE_FALSE(manager.hasGuild("g_404"));
}

TEST_CASE("GuildManager upsertGuild accepts an explicit visibility") {
    guild::GuildManager manager;

    guild::Guild& g = manager.upsertGuild("g_1", "First", "u_1", guild::GuildVisibility::PRIVATE);
    REQUIRE(g.visibility == guild::GuildVisibility::PRIVATE);
}

TEST_CASE("GuildManager setGuildVisibility updates the cached guild") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");

    manager.setGuildVisibility("g_1", guild::GuildVisibility::APPLICATION);
    REQUIRE(manager.getGuild("g_1")->visibility == guild::GuildVisibility::APPLICATION);
}

TEST_CASE("GuildManager setGuildVisibility no-ops for an unknown guild") {
    guild::GuildManager manager;
    manager.setGuildVisibility("g_404", guild::GuildVisibility::PRIVATE);
    REQUIRE(manager.getGuild("g_404") == nullptr);
}

TEST_CASE("toString/guildVisibilityFromString round-trip every visibility") {
    using guild::GuildVisibility;
    CHECK(guild::toString(GuildVisibility::OPEN) == "open");
    CHECK(guild::toString(GuildVisibility::APPLICATION) == "application");
    CHECK(guild::toString(GuildVisibility::PRIVATE) == "private");

    CHECK(guild::guildVisibilityFromString("open") == GuildVisibility::OPEN);
    CHECK(guild::guildVisibilityFromString("application") == GuildVisibility::APPLICATION);
    CHECK(guild::guildVisibilityFromString("private") == GuildVisibility::PRIVATE);
    CHECK_FALSE(guild::guildVisibilityFromString("bogus").has_value());
}

TEST_CASE("GuildManager upsertGuild called twice with the same id overwrites, not duplicates") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.upsertGuild("g_1", "Renamed", "u_1");

    REQUIRE(manager.listGuilds().size() == 1);
    REQUIRE(manager.getGuild("g_1")->name == "Renamed");
}

TEST_CASE("GuildManager getGuild returns the stored guild") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");

    const guild::Guild* guild = manager.getGuild("g_1");
    REQUIRE(guild != nullptr);
    REQUIRE(guild->name == "First");

    REQUIRE(manager.getGuild("g_404") == nullptr);
}

TEST_CASE("GuildManager listGuilds returns every guild") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.upsertGuild("g_2", "Second", "u_2");

    const auto guilds = manager.listGuilds();
    REQUIRE(guilds.size() == 2);
}

TEST_CASE("GuildManager upsertChannel stores a channel scoped to its guild") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");

    guild::Channel& c1 = manager.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    guild::Channel& c2 =
        manager.upsertChannel("c_2", "g_1", "voice-lounge", guild::ChannelType::VOICE);

    REQUIRE(c1.id == "c_1");
    REQUIRE(c1.guild_id == "g_1");
    REQUIRE(c1.name == "general");
    REQUIRE(c1.type == guild::ChannelType::TEXT);

    REQUIRE(c2.id == "c_2");
    REQUIRE(c2.type == guild::ChannelType::VOICE);

    REQUIRE(manager.hasChannel("c_1"));
    REQUIRE(manager.hasChannel("c_2"));
}

TEST_CASE("GuildManager listChannels only returns channels for the requested guild") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.upsertGuild("g_2", "Second", "u_2");

    manager.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    manager.upsertChannel("c_2", "g_1", "random", guild::ChannelType::TEXT);
    manager.upsertChannel("c_3", "g_2", "other-guild-channel", guild::ChannelType::TEXT);

    const auto channels = manager.listChannels("g_1");
    REQUIRE(channels.size() == 2);

    const auto other = manager.listChannels("g_2");
    REQUIRE(other.size() == 1);
}

TEST_CASE("GuildManager deleteChannel removes only that channel") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    manager.upsertChannel("c_2", "g_1", "random", guild::ChannelType::TEXT);

    REQUIRE(manager.deleteChannel("c_1"));
    REQUIRE_FALSE(manager.hasChannel("c_1"));
    REQUIRE(manager.hasChannel("c_2"));

    REQUIRE_FALSE(manager.deleteChannel("c_1")); // already gone
}

TEST_CASE("GuildManager deleteGuild cascades to delete its channels") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.upsertGuild("g_2", "Second", "u_2");
    manager.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    manager.upsertChannel("c_2", "g_1", "random", guild::ChannelType::TEXT);
    manager.upsertChannel("c_3", "g_2", "unrelated", guild::ChannelType::TEXT);

    REQUIRE(manager.deleteGuild("g_1"));

    REQUIRE_FALSE(manager.hasGuild("g_1"));
    REQUIRE(manager.hasGuild("g_2"));
    REQUIRE(manager.listChannels("g_1").empty());
    REQUIRE(manager.listChannels("g_2").size() == 1); // untouched

    REQUIRE_FALSE(manager.deleteGuild("g_1")); // already gone
}

TEST_CASE("GuildManager isOwner reflects the guild's owner_id") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");

    REQUIRE(manager.isOwner("g_1", "u_1"));
    REQUIRE_FALSE(manager.isOwner("g_1", "u_2"));
    REQUIRE_FALSE(manager.isOwner("g_404", "u_1")); // unknown guild
}

TEST_CASE("GuildManager getMemberRank reflects setMemberRank/removeMember") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");

    REQUIRE_FALSE(manager.getMemberRank("g_1", "u_1").has_value());

    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);
    REQUIRE(manager.getMemberRank("g_1", "u_1") == guild::kOwnerRank);

    manager.removeMember("g_1", "u_1");
    REQUIRE_FALSE(manager.getMemberRank("g_1", "u_1").has_value());
}

TEST_CASE("GuildManager deleteGuild also clears that guild's member ranks") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);

    manager.deleteGuild("g_1");

    // Re-creating the guild under the same id must not resurrect stale ranks.
    manager.upsertGuild("g_1", "First", "u_1");
    REQUIRE_FALSE(manager.getMemberRank("g_1", "u_1").has_value());
}

TEST_CASE("GuildManager isOfficerOrAbove requires kOfficerRank or higher") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.setMemberRank("g_1", "u_officer", guild::kOfficerRank);
    manager.setMemberRank("g_1", "u_crew", guild::kMemberRank);

    REQUIRE(manager.isOfficerOrAbove("g_1", "u_officer"));
    REQUIRE_FALSE(manager.isOfficerOrAbove("g_1", "u_crew"));
    REQUIRE_FALSE(manager.isOfficerOrAbove("g_1", "u_stranger")); // no cached rank at all
}

TEST_CASE("GuildManager canCreateChannel widens to officer-or-above") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);
    manager.setMemberRank("g_1", "u_officer", guild::kOfficerRank);
    manager.setMemberRank("g_1", "u_crew", guild::kMemberRank);

    REQUIRE(manager.canCreateChannel("g_1", "u_1"));
    REQUIRE(manager.canCreateChannel("g_1", "u_officer"));
    REQUIRE_FALSE(manager.canCreateChannel("g_1", "u_crew"));
}

TEST_CASE("GuildManager getGuildIdsForUser reflects setMemberRank/removeMember across guilds") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.upsertGuild("g_2", "Second", "u_2");

    REQUIRE(manager.getGuildIdsForUser("u_1").empty());

    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);
    manager.setMemberRank("g_2", "u_1", guild::kMemberRank);
    // Setting a rank twice for the same (guild, user) must not duplicate the entry.
    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);

    auto ids = manager.getGuildIdsForUser("u_1");
    REQUIRE(ids.size() == 2);
    REQUIRE(std::find(ids.begin(), ids.end(), "g_1") != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), "g_2") != ids.end());

    manager.removeMember("g_1", "u_1");
    ids = manager.getGuildIdsForUser("u_1");
    REQUIRE(ids.size() == 1);
    REQUIRE(ids[0] == "g_2");
}

TEST_CASE("GuildManager deleteGuild also removes it from every member's getGuildIdsForUser") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.upsertGuild("g_2", "Second", "u_1");
    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);
    manager.setMemberRank("g_2", "u_1", guild::kOwnerRank);

    manager.deleteGuild("g_1");

    const auto ids = manager.getGuildIdsForUser("u_1");
    REQUIRE(ids.size() == 1);
    REQUIRE(ids[0] == "g_2");
}

TEST_CASE("GuildManager canDeleteChannel and canSetMemberRole stay owner-only") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);
    manager.setMemberRank("g_1", "u_officer", guild::kOfficerRank);

    REQUIRE(manager.canDeleteChannel("g_1", "u_1"));
    REQUIRE_FALSE(manager.canDeleteChannel("g_1", "u_officer"));

    REQUIRE(manager.canSetMemberRole("g_1", "u_1"));
    REQUIRE_FALSE(manager.canSetMemberRole("g_1", "u_officer"));
}

TEST_CASE("GuildManager canCreateInvite and canApproveJoinRequest widen to officer-or-above") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);
    manager.setMemberRank("g_1", "u_officer", guild::kOfficerRank);
    manager.setMemberRank("g_1", "u_crew", guild::kMemberRank);

    REQUIRE(manager.canCreateInvite("g_1", "u_1"));
    REQUIRE(manager.canCreateInvite("g_1", "u_officer"));
    REQUIRE_FALSE(manager.canCreateInvite("g_1", "u_crew"));

    REQUIRE(manager.canApproveJoinRequest("g_1", "u_1"));
    REQUIRE(manager.canApproveJoinRequest("g_1", "u_officer"));
    REQUIRE_FALSE(manager.canApproveJoinRequest("g_1", "u_crew"));
}

TEST_CASE("GuildManager canSetGuildVisibility stays owner-only") {
    guild::GuildManager manager;
    manager.upsertGuild("g_1", "First", "u_1");
    manager.setMemberRank("g_1", "u_1", guild::kOwnerRank);
    manager.setMemberRank("g_1", "u_officer", guild::kOfficerRank);

    REQUIRE(manager.canSetGuildVisibility("g_1", "u_1"));
    REQUIRE_FALSE(manager.canSetGuildVisibility("g_1", "u_officer"));
}
