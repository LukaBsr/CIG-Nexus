#include <catch2/catch_test_macros.hpp>

#include "guild/GuildManager.hpp"
#include "guild/RoleRank.hpp"
#include "persistence/MessagePersistenceWorker.hpp"
#include "protocol/handlers/ChannelHandler.hpp"
#include "session/SessionManager.hpp"

#include "../http/FakeInternalApiClient.hpp"

#include <algorithm>

namespace {

protocol::Message make_message(const std::string& type, nlohmann::json extra = {}) {
    protocol::Message message;
    message.type = type;
    extra["type"] = type;
    message.payload = extra;
    return message;
}

struct Fixture {
    session::SessionManager sessions;
    guild::GuildManager guilds;
    test_helpers::FakeInternalApiClient api;
    protocol::ChannelHandler handler;

    Fixture() {
        handler.setSessionManager(&sessions);
        handler.setGuildManager(&guilds);
        handler.setInternalApiClient(&api);
    }

    session::Session& identify(int fd, const std::string& username) {
        session::Session& session = sessions.createSession(fd);
        session.username = username;
        session.user_id = "u_" + std::to_string(fd);
        return session;
    }

    // canCreateChannel/canDeleteChannel now read GuildManager's role_rank
    // cache (docs/guilds/social-presence-design.md §2.2), not owner_id directly —
    // tests exercising owner-gated behavior must seed it explicitly, same
    // as they already seed session-level guild membership.
    void makeOwner(const std::string& guild_id, const std::string& user_id) {
        guilds.setMemberRank(guild_id, user_id, guild::kOwnerRank);
    }
};

bool contains(const std::vector<int>& fds, int fd) {
    return std::find(fds.begin(), fds.end(), fd) != fds.end();
}

} // namespace

TEST_CASE("ChannelHandler LIST_CHANNELS returns channels for a guild member") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.addGuildMembership(1, "g_1");

    const auto response =
        f.handler.handleListChannels(make_message("LIST_CHANNELS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.type == "CHANNEL_LIST");
    REQUIRE(response.payload["channels"].size() == 1);
    REQUIRE(response.payload["channels"][0]["channel_id"] == "c_1");
}

TEST_CASE("ChannelHandler LIST_CHANNELS rejects a non-member") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");

    const auto response =
        f.handler.handleListChannels(make_message("LIST_CHANNELS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_MEMBER");
}

TEST_CASE("ChannelHandler LIST_CHANNELS rejects unknown guild") {
    Fixture f;
    f.identify(1, "alice");

    const auto response =
        f.handler.handleListChannels(make_message("LIST_CHANNELS", {{"guild_id", "g_404"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("ChannelHandler CREATE_CHANNEL by the owner notifies every guild member") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.makeOwner("g_1", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");

    const auto response = f.handler.handleCreateChannel(
        make_message("CREATE_CHANNEL",
                     {{"guild_id", "g_1"}, {"name", "general"}, {"channel_type", "TEXT"}}),
        1);

    REQUIRE(response.type == "CHANNEL_CREATED");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.target_fds.size() == 2);
    REQUIRE(contains(response.target_fds, 1));
    REQUIRE(contains(response.target_fds, 2));
    REQUIRE(response.payload["channel_id"] == "c_fake_1"); // assigned by (fake) InternalApiClient
    REQUIRE(response.payload["channel_type"] == "TEXT");
    REQUIRE(f.guilds.hasChannel("c_fake_1"));
}

TEST_CASE("ChannelHandler CREATE_CHANNEL rejects a member below officer rank") {
    Fixture f;
    f.identify(1, "crew-member");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.sessions.addGuildMembership(1, "g_1");

    const auto response = f.handler.handleCreateChannel(
        make_message("CREATE_CHANNEL",
                     {{"guild_id", "g_1"}, {"name", "general"}, {"channel_type", "TEXT"}}),
        1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OFFICER");
}

TEST_CASE("ChannelHandler CREATE_CHANNEL rejects an invalid channel_type") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);

    const auto response = f.handler.handleCreateChannel(
        make_message("CREATE_CHANNEL",
                     {{"guild_id", "g_1"}, {"name", "general"}, {"channel_type", "VIDEO"}}),
        1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChannelHandler CREATE_CHANNEL returns INTERNAL_ERROR without mutating state when the "
          "internal API call fails") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.makeOwner("g_1", owner.user_id);
    f.api.fail_create_channel = true;

    const auto response = f.handler.handleCreateChannel(
        make_message("CREATE_CHANNEL",
                     {{"guild_id", "g_1"}, {"name", "general"}, {"channel_type", "TEXT"}}),
        1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE(f.guilds.listChannels("g_1").empty());
}

TEST_CASE("ChannelHandler DELETE_CHANNEL clears the active channel for members who had it") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.makeOwner("g_1", owner.user_id);
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");
    f.sessions.setActiveChannel(2, "c_1");

    const auto response = f.handler.handleDeleteChannel(
        make_message("DELETE_CHANNEL", {{"guild_id", "g_1"}, {"channel_id", "c_1"}}), 1);

    REQUIRE(response.type == "CHANNEL_DELETED");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.target_fds.size() == 2);
    REQUIRE_FALSE(f.guilds.hasChannel("c_1"));
    REQUIRE(f.sessions.getSession(2)->active_channel_id.empty());
}

TEST_CASE("ChannelHandler DELETE_CHANNEL rejects a non-owner") {
    Fixture f;
    f.identify(1, "not-owner");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.addGuildMembership(1, "g_1");

    const auto response = f.handler.handleDeleteChannel(
        make_message("DELETE_CHANNEL", {{"guild_id", "g_1"}, {"channel_id", "c_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OWNER");
    REQUIRE(f.guilds.hasChannel("c_1")); // unchanged
}

TEST_CASE("ChannelHandler DELETE_CHANNEL rejects an unknown channel") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);

    const auto response = f.handler.handleDeleteChannel(
        make_message("DELETE_CHANNEL", {{"guild_id", "g_1"}, {"channel_id", "c_404"}}), 1);

    REQUIRE(response.payload["code"] == "CHANNEL_NOT_FOUND");
}

TEST_CASE("ChannelHandler DELETE_CHANNEL returns INTERNAL_ERROR without mutating state when the "
          "internal API call fails") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.makeOwner("g_1", owner.user_id);
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.api.fail_delete_channel = true;

    const auto response = f.handler.handleDeleteChannel(
        make_message("DELETE_CHANNEL", {{"guild_id", "g_1"}, {"channel_id", "c_1"}}), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE(f.guilds.hasChannel("c_1")); // unchanged
}

TEST_CASE("ChannelHandler JOIN_CHANNEL sets the active channel, replacing any previous one") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.guilds.upsertChannel("c_2", "g_1", "random", guild::ChannelType::TEXT);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.setActiveChannel(1, "c_1");

    const auto response =
        f.handler.handleJoinChannel(make_message("JOIN_CHANNEL", {{"channel_id", "c_2"}}), 1);

    REQUIRE(response.type == "CHANNEL_JOINED");
    REQUIRE(response.payload["channel_id"] == "c_2");
    REQUIRE(f.sessions.getSession(1)->active_channel_id == "c_2");
}

TEST_CASE("ChannelHandler JOIN_CHANNEL rejects a non-member of the channel's guild") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);

    const auto response =
        f.handler.handleJoinChannel(make_message("JOIN_CHANNEL", {{"channel_id", "c_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_MEMBER");
}

TEST_CASE("ChannelHandler JOIN_CHANNEL rejects voice channels this iteration") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "voice-lounge", guild::ChannelType::VOICE);
    f.sessions.addGuildMembership(1, "g_1");

    const auto response =
        f.handler.handleJoinChannel(make_message("JOIN_CHANNEL", {{"channel_id", "c_1"}}), 1);

    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
    REQUIRE(f.sessions.getSession(1)->active_channel_id.empty());
}

TEST_CASE("ChannelHandler LEAVE_CHANNEL clears the active channel") {
    Fixture f;
    f.identify(1, "alice");
    f.sessions.setActiveChannel(1, "c_1");

    const auto response = f.handler.handleLeaveChannel(make_message("LEAVE_CHANNEL"), 1);

    REQUIRE(response.type == "CHANNEL_LEFT");
    REQUIRE(response.payload["channel_id"] == "c_1");
    REQUIRE(f.sessions.getSession(1)->active_channel_id.empty());
}

TEST_CASE("ChannelHandler LEAVE_CHANNEL rejects a connection with no active channel") {
    Fixture f;
    f.identify(1, "alice");

    const auto response = f.handler.handleLeaveChannel(make_message("LEAVE_CHANNEL"), 1);

    REQUIRE(response.payload["code"] == "NOT_IN_CHANNEL");
}

TEST_CASE("ChannelHandler CHANNEL_MESSAGE delivers only to connections with that active channel") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(2, "bob");
    f.identify(3, "carol");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.guilds.upsertChannel("c_2", "g_1", "random", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(1, "c_1");
    f.sessions.setActiveChannel(2, "c_1");
    f.sessions.setActiveChannel(3, "c_2"); // different channel, should not receive it

    const auto response =
        f.handler.handleChannelMessage(make_message("CHANNEL_MESSAGE", {{"content", "hello"}}), 1);

    REQUIRE(response.type == "CHANNEL_MESSAGE");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.target_fds.size() == 2);
    REQUIRE(contains(response.target_fds, 1));
    REQUIRE(contains(response.target_fds, 2));
    REQUIRE_FALSE(contains(response.target_fds, 3));
    REQUIRE(response.payload["channel_id"] == "c_1");
    REQUIRE(response.payload["guild_id"] == "g_1");
    REQUIRE(response.payload["username"] == "alice");
    REQUIRE(response.payload["content"] == "hello");
}

TEST_CASE("ChannelHandler CHANNEL_MESSAGE message_id increments across calls") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(1, "c_1");

    const auto r1 =
        f.handler.handleChannelMessage(make_message("CHANNEL_MESSAGE", {{"content", "a"}}), 1);
    const auto r2 =
        f.handler.handleChannelMessage(make_message("CHANNEL_MESSAGE", {{"content", "b"}}), 1);

    REQUIRE(r2.payload["message_id"].get<int>() == r1.payload["message_id"].get<int>() + 1);
}

TEST_CASE("ChannelHandler CHANNEL_MESSAGE rejects a connection with no active channel") {
    Fixture f;
    f.identify(1, "alice");

    const auto response =
        f.handler.handleChannelMessage(make_message("CHANNEL_MESSAGE", {{"content", "hello"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_IN_CHANNEL");
}

TEST_CASE("ChannelHandler CHANNEL_MESSAGE rejects empty content") {
    Fixture f;
    f.identify(1, "alice");
    f.sessions.setActiveChannel(1, "c_1");

    const auto response =
        f.handler.handleChannelMessage(make_message("CHANNEL_MESSAGE", {{"content", ""}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChannelHandler CHANNEL_MESSAGE enqueues a persisted message with the channel_id") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(1, "c_1");

    persistence::MessagePersistenceWorker worker(&f.api);
    worker.start();
    f.handler.setMessagePersistenceWorker(&worker);

    const auto response =
        f.handler.handleChannelMessage(make_message("CHANNEL_MESSAGE", {{"content", "hello"}}), 1);
    worker.stop(); // drains the queue before returning, so this establishes a happens-before

    REQUIRE(f.api.created_messages.size() == 1);
    REQUIRE(f.api.created_messages[0].channel_id == "c_1");
    REQUIRE(f.api.created_messages[0].user_id == "u_1");
    REQUIRE(f.api.created_messages[0].content == "hello");
    REQUIRE(f.api.created_messages[0].message_id == response.payload["message_id"].get<int>());
}

TEST_CASE("ChannelHandler seedMessageCounter seeds the next CHANNEL_MESSAGE message_id") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(1, "c_1");
    f.handler.seedMessageCounter(9);

    const auto response =
        f.handler.handleChannelMessage(make_message("CHANNEL_MESSAGE", {{"content", "hello"}}), 1);

    REQUIRE(response.payload["message_id"].get<int>() == 10);
}

TEST_CASE("ChannelHandler FETCH_HISTORY returns lobby history when channel_id is omitted") {
    Fixture f;
    f.identify(1, "alice");
    f.api.history_page_to_return = http::HistoryPage{
        {http::WireMessage{1, std::nullopt, 1000, "u_2", "bob", "hi", std::nullopt, std::nullopt},
         http::WireMessage{2, std::nullopt, 2000, "u_1", "alice", "hello", std::nullopt,
                           std::nullopt}},
        false};

    const auto response = f.handler.handleFetchHistory(make_message("FETCH_HISTORY"), 1);

    REQUIRE(response.type == "MESSAGE_HISTORY");
    REQUIRE(response.payload["channel_id"].is_null());
    REQUIRE(response.payload["has_more"] == false);
    REQUIRE(response.payload["messages"].size() == 2);
    REQUIRE(response.payload["messages"][0]["content"] == "hi");
    REQUIRE(response.payload["messages"][1]["username"] == "alice");
}

TEST_CASE("ChannelHandler FETCH_HISTORY returns channel history for a guild member") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.addGuildMembership(1, "g_1");
    f.api.history_page_to_return =
        http::HistoryPage{{http::WireMessage{5, "c_1", 3000, "u_1", "alice", "in channel",
                                             std::nullopt, std::nullopt}},
                          true};

    const auto response =
        f.handler.handleFetchHistory(make_message("FETCH_HISTORY", {{"channel_id", "c_1"}}), 1);

    REQUIRE(response.type == "MESSAGE_HISTORY");
    REQUIRE(response.payload["channel_id"] == "c_1");
    REQUIRE(response.payload["has_more"] == true);
    REQUIRE(response.payload["messages"].size() == 1);
    REQUIRE(response.payload["messages"][0]["message_id"] == 5);
}

TEST_CASE("ChannelHandler FETCH_HISTORY rejects a non-member of the channel's guild") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);

    const auto response =
        f.handler.handleFetchHistory(make_message("FETCH_HISTORY", {{"channel_id", "c_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_MEMBER");
}

TEST_CASE("ChannelHandler FETCH_HISTORY rejects an unknown channel") {
    Fixture f;
    f.identify(1, "alice");

    const auto response =
        f.handler.handleFetchHistory(make_message("FETCH_HISTORY", {{"channel_id", "c_404"}}), 1);

    REQUIRE(response.payload["code"] == "CHANNEL_NOT_FOUND");
}

TEST_CASE("ChannelHandler FETCH_HISTORY requires identification") {
    Fixture f;

    const auto response = f.handler.handleFetchHistory(make_message("FETCH_HISTORY"), 1);

    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("ChannelHandler FETCH_HISTORY rejects a limit above the maximum") {
    Fixture f;
    f.identify(1, "alice");

    const auto response =
        f.handler.handleFetchHistory(make_message("FETCH_HISTORY", {{"limit", 101}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChannelHandler FETCH_HISTORY rejects a non-integer before_seq") {
    Fixture f;
    f.identify(1, "alice");

    const auto response = f.handler.handleFetchHistory(
        make_message("FETCH_HISTORY", {{"before_seq", "not-a-number"}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChannelHandler FETCH_HISTORY returns INTERNAL_ERROR when the internal API call fails") {
    Fixture f;
    f.identify(1, "alice");
    f.api.fail_fetch_messages = true;

    const auto response = f.handler.handleFetchHistory(make_message("FETCH_HISTORY"), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
}
