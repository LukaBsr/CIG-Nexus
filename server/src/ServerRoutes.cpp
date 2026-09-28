#include "Server.hpp"
#include "protocol/MessageBuilders.hpp"

#include <vector>

void Server::registerRoutes() {
    // docs/guilds/social-presence-design.md §1.9/§6 step 5: most registrations below
    // wrap their handler's single Message in a one-element vector — the
    // dispatcher contract is std::vector<Message>, but only the handlers
    // that actually need to notify two different recipients with two
    // different payloads (JOIN_VIA_INVITE's application-mode diversion,
    // REQUEST_JOIN, APPROVE_JOIN_REQUEST, REJECT_JOIN_REQUEST) return the
    // vector directly instead of wrapping.
    dispatcher_.registerHandler("HELLO", [this](const protocol::Message& msg, int /*fd*/) {
        return std::vector<protocol::Message>{hello_handler_.handle(msg)};
    });

    dispatcher_.registerHandler("CHAT_MESSAGE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{chat_handler_.handle(msg, fd)};
    });

    dispatcher_.registerHandler("IDENTIFY", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{identify_handler_.handle(msg, fd)};
    });

    dispatcher_.registerHandler("CREATE_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleCreateGuild(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_GUILDS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleListGuilds(msg, fd)};
    });

    dispatcher_.registerHandler("JOIN_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleJoinGuild(msg, fd)};
    });

    dispatcher_.registerHandler("LEAVE_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleLeaveGuild(msg, fd)};
    });

    dispatcher_.registerHandler("DELETE_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleDeleteGuild(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_MEMBERS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleListMembers(msg, fd)};
    });

    dispatcher_.registerHandler("SET_MEMBER_ROLE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleSetMemberRole(msg, fd)};
    });

    dispatcher_.registerHandler(
        "SET_GUILD_VISIBILITY", [this](const protocol::Message& msg, int fd) {
            return std::vector<protocol::Message>{guild_handler_.handleSetGuildVisibility(msg, fd)};
        });

    dispatcher_.registerHandler("LIST_CHANNELS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleListChannels(msg, fd)};
    });

    dispatcher_.registerHandler("CREATE_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleCreateChannel(msg, fd)};
    });

    dispatcher_.registerHandler("DELETE_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleDeleteChannel(msg, fd)};
    });

    dispatcher_.registerHandler("JOIN_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleJoinChannel(msg, fd)};
    });

    dispatcher_.registerHandler("LEAVE_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleLeaveChannel(msg, fd)};
    });

    dispatcher_.registerHandler("CHANNEL_MESSAGE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleChannelMessage(msg, fd)};
    });

    // docs/social/friends-dms-design.md §3.5: FETCH_HISTORY gains a third,
    // mutually-exclusive scope (peer_id, alongside channel_id/lobby) —
    // dispatched here to DMHandler instead of ChannelHandler based on
    // which field the payload carries, rather than teaching ChannelHandler
    // about DMs or vice versa.
    dispatcher_.registerHandler("FETCH_HISTORY", [this](const protocol::Message& msg, int fd) {
        const bool has_peer_id = msg.payload.is_object() && msg.payload.contains("peer_id") &&
                                 !msg.payload["peer_id"].is_null();
        const bool has_channel_id = msg.payload.is_object() && msg.payload.contains("channel_id") &&
                                    !msg.payload["channel_id"].is_null();
        if (has_peer_id && has_channel_id) {
            protocol::Message error;
            error.type = "ERROR";
            error.payload = protocol::make_error(
                "MALFORMED_MESSAGE",
                "FETCH_HISTORY: channel_id and peer_id are mutually exclusive");
            return std::vector<protocol::Message>{error};
        }
        if (has_peer_id) {
            return std::vector<protocol::Message>{dm_handler_.handleFetchHistory(msg, fd)};
        }
        return std::vector<protocol::Message>{channel_handler_.handleFetchHistory(msg, fd)};
    });

    dispatcher_.registerHandler("CREATE_INVITE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{invite_handler_.handleCreateInvite(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_INVITES", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{invite_handler_.handleListInvites(msg, fd)};
    });

    dispatcher_.registerHandler("REVOKE_INVITE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{invite_handler_.handleRevokeInvite(msg, fd)};
    });

    dispatcher_.registerHandler("JOIN_VIA_INVITE", [this](const protocol::Message& msg, int fd) {
        return invite_handler_.handleJoinViaInvite(msg, fd);
    });

    dispatcher_.registerHandler("REQUEST_JOIN", [this](const protocol::Message& msg, int fd) {
        return join_request_handler_.handleRequestJoin(msg, fd);
    });

    dispatcher_.registerHandler("LIST_JOIN_REQUESTS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{
            join_request_handler_.handleListJoinRequests(msg, fd)};
    });

    dispatcher_.registerHandler("APPROVE_JOIN_REQUEST",
                                [this](const protocol::Message& msg, int fd) {
                                    return join_request_handler_.handleApproveJoinRequest(msg, fd);
                                });

    dispatcher_.registerHandler("REJECT_JOIN_REQUEST",
                                [this](const protocol::Message& msg, int fd) {
                                    return join_request_handler_.handleRejectJoinRequest(msg, fd);
                                });

    dispatcher_.registerHandler("SEND_FRIEND_REQUEST",
                                [this](const protocol::Message& msg, int fd) {
                                    return friend_handler_.handleSendFriendRequest(msg, fd);
                                });

    dispatcher_.registerHandler("ADD_FRIEND_BY_CODE", [this](const protocol::Message& msg, int fd) {
        return friend_handler_.handleAddFriendByCode(msg, fd);
    });

    dispatcher_.registerHandler("ACCEPT_FRIEND_REQUEST",
                                [this](const protocol::Message& msg, int fd) {
                                    return friend_handler_.handleAcceptFriendRequest(msg, fd);
                                });

    dispatcher_.registerHandler("REJECT_FRIEND_REQUEST",
                                [this](const protocol::Message& msg, int fd) {
                                    return friend_handler_.handleRejectFriendRequest(msg, fd);
                                });

    dispatcher_.registerHandler("CANCEL_FRIEND_REQUEST",
                                [this](const protocol::Message& msg, int fd) {
                                    return friend_handler_.handleCancelFriendRequest(msg, fd);
                                });

    dispatcher_.registerHandler("REMOVE_FRIEND", [this](const protocol::Message& msg, int fd) {
        return friend_handler_.handleRemoveFriend(msg, fd);
    });

    dispatcher_.registerHandler("LIST_FRIENDS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{friend_handler_.handleListFriends(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_FRIEND_REQUESTS", [this](const protocol::Message& msg,
                                                               int fd) {
        return std::vector<protocol::Message>{friend_handler_.handleListFriendRequests(msg, fd)};
    });

    dispatcher_.registerHandler("FETCH_FRIEND_CODE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{friend_handler_.handleFetchFriendCode(msg, fd)};
    });

    dispatcher_.registerHandler("REGENERATE_FRIEND_CODE", [this](const protocol::Message& msg,
                                                                 int fd) {
        return std::vector<protocol::Message>{friend_handler_.handleRegenerateFriendCode(msg, fd)};
    });

    dispatcher_.registerHandler("BLOCK_USER", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{block_handler_.handleBlockUser(msg, fd)};
    });

    dispatcher_.registerHandler("UNBLOCK_USER", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{block_handler_.handleUnblockUser(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_BLOCKS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{block_handler_.handleListBlocks(msg, fd)};
    });

    dispatcher_.registerHandler("DM_SEND", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{dm_handler_.handleDmSend(msg, fd)};
    });

    dispatcher_.registerHandler(
        "LIST_DM_CONVERSATIONS", [this](const protocol::Message& msg, int fd) {
            return std::vector<protocol::Message>{dm_handler_.handleListDmConversations(msg, fd)};
        });
}
