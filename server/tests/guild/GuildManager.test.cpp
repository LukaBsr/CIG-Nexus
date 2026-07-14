#include <catch2/catch_test_macros.hpp>

#include "guild/GuildManager.hpp"

TEST_CASE("GuildManager creates guilds with incrementing ids") {
    guild::GuildManager manager;

    guild::Guild& g1 = manager.createGuild("First", "u_1");
    guild::Guild& g2 = manager.createGuild("Second", "u_2");

    REQUIRE(g1.id == "g_1");
    REQUIRE(g1.name == "First");
    REQUIRE(g1.owner_id == "u_1");
    REQUIRE(g1.created_at > 0);

    REQUIRE(g2.id == "g_2");
    REQUIRE(manager.hasGuild("g_1"));
    REQUIRE(manager.hasGuild("g_2"));
    REQUIRE_FALSE(manager.hasGuild("g_404"));
}

TEST_CASE("GuildManager getGuild returns the stored guild") {
    guild::GuildManager manager;
    manager.createGuild("First", "u_1");

    const guild::Guild* guild = manager.getGuild("g_1");
    REQUIRE(guild != nullptr);
    REQUIRE(guild->name == "First");

    REQUIRE(manager.getGuild("g_404") == nullptr);
}

TEST_CASE("GuildManager listGuilds returns every guild") {
    guild::GuildManager manager;
    manager.createGuild("First", "u_1");
    manager.createGuild("Second", "u_2");

    const auto guilds = manager.listGuilds();
    REQUIRE(guilds.size() == 2);
}

TEST_CASE("GuildManager creates channels with incrementing ids scoped to a guild") {
    guild::GuildManager manager;
    manager.createGuild("First", "u_1");

    guild::Channel& c1 = manager.createChannel("g_1", "general", guild::ChannelType::TEXT);
    guild::Channel& c2 = manager.createChannel("g_1", "voice-lounge", guild::ChannelType::VOICE);

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
    manager.createGuild("First", "u_1");
    manager.createGuild("Second", "u_2");

    manager.createChannel("g_1", "general", guild::ChannelType::TEXT);
    manager.createChannel("g_1", "random", guild::ChannelType::TEXT);
    manager.createChannel("g_2", "other-guild-channel", guild::ChannelType::TEXT);

    const auto channels = manager.listChannels("g_1");
    REQUIRE(channels.size() == 2);

    const auto other = manager.listChannels("g_2");
    REQUIRE(other.size() == 1);
}

TEST_CASE("GuildManager deleteChannel removes only that channel") {
    guild::GuildManager manager;
    manager.createGuild("First", "u_1");
    manager.createChannel("g_1", "general", guild::ChannelType::TEXT);
    manager.createChannel("g_1", "random", guild::ChannelType::TEXT);

    REQUIRE(manager.deleteChannel("c_1"));
    REQUIRE_FALSE(manager.hasChannel("c_1"));
    REQUIRE(manager.hasChannel("c_2"));

    REQUIRE_FALSE(manager.deleteChannel("c_1")); // already gone
}

TEST_CASE("GuildManager deleteGuild cascades to delete its channels") {
    guild::GuildManager manager;
    manager.createGuild("First", "u_1");
    manager.createGuild("Second", "u_2");
    manager.createChannel("g_1", "general", guild::ChannelType::TEXT);
    manager.createChannel("g_1", "random", guild::ChannelType::TEXT);
    manager.createChannel("g_2", "unrelated", guild::ChannelType::TEXT);

    REQUIRE(manager.deleteGuild("g_1"));

    REQUIRE_FALSE(manager.hasGuild("g_1"));
    REQUIRE(manager.hasGuild("g_2"));
    REQUIRE(manager.listChannels("g_1").empty());
    REQUIRE(manager.listChannels("g_2").size() == 1); // untouched

    REQUIRE_FALSE(manager.deleteGuild("g_1")); // already gone
}

TEST_CASE("GuildManager isOwner reflects the guild's owner_id") {
    guild::GuildManager manager;
    manager.createGuild("First", "u_1");

    REQUIRE(manager.isOwner("g_1", "u_1"));
    REQUIRE_FALSE(manager.isOwner("g_1", "u_2"));
    REQUIRE_FALSE(manager.isOwner("g_404", "u_1")); // unknown guild
}

TEST_CASE("GuildManager canCreateChannel and canDeleteChannel are owner-only today") {
    guild::GuildManager manager;
    manager.createGuild("First", "u_1");

    REQUIRE(manager.canCreateChannel("g_1", "u_1"));
    REQUIRE_FALSE(manager.canCreateChannel("g_1", "u_2"));

    REQUIRE(manager.canDeleteChannel("g_1", "u_1"));
    REQUIRE_FALSE(manager.canDeleteChannel("g_1", "u_2"));
}
