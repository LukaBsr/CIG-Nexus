#include "protocol/handlers/DMHandler.hpp"

#include "http/InternalApiClient.hpp"
#include "persistence/MessagePersistenceWorker.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

#include <algorithm>
#include <ctime>

namespace protocol {

namespace {
// Matches web/app/internal/messages/route.ts's DEFAULT_LIMIT/MAX_LIMIT —
// same as ChannelHandler::handleFetchHistory.
constexpr int kDefaultHistoryLimit = 50;
constexpr int kMaxHistoryLimit = 100;

bool contains(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool intersects(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    for (const auto& id : a) {
        if (contains(b, id)) {
            return true;
        }
    }
    return false;
}
} // namespace

void DMHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void DMHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

void DMHandler::setMessagePersistenceWorker(persistence::MessagePersistenceWorker* worker) {
    message_worker_ = worker;
}

void DMHandler::seedMessageCounter(std::optional<int> last_seq) {
    if (last_seq.has_value()) {
        message_counter_.store(*last_seq);
    }
}

Message DMHandler::makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

const session::Session* DMHandler::requireIdentified(int fd) const {
    if (!session_manager_) {
        return nullptr;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

// docs/social/friends-dms-design.md §3.2/§3.3. Every check here is
// in-memory except the two peer-side fallbacks, which only fire when the
// peer has zero active connections — the common case (peer online) never
// makes a live call at all.
bool DMHandler::canSendDm(const session::Session* session, const std::string& peer_user_id) const {
    const bool is_friend = contains(session->friend_ids, peer_user_id);
    const session::Session* peer_session = session_manager_->getSessionByUserId(peer_user_id);

    bool shares_guild = is_friend; // short-circuit: no need to check if already permitted
    if (!is_friend) {
        if (peer_session) {
            shares_guild = intersects(session->guild_ids, peer_session->guild_ids);
        } else if (internal_api_client_) {
            if (const auto peer_guild_ids = internal_api_client_->fetchGuildIdsForUser(peer_user_id)) {
                shares_guild = intersects(session->guild_ids, *peer_guild_ids);
            }
        }
    }

    if (!is_friend && !shares_guild) {
        return false;
    }

    const bool caller_blocked_peer = contains(session->blocked_user_ids, peer_user_id);
    bool peer_blocked_caller = false;
    if (peer_session) {
        peer_blocked_caller = contains(peer_session->blocked_user_ids, session->user_id);
    } else if (internal_api_client_) {
        if (const auto peer_blocks = internal_api_client_->fetchBlocks(peer_user_id)) {
            for (const auto& block : *peer_blocks) {
                if (block.user_id == session->user_id) {
                    peer_blocked_caller = true;
                    break;
                }
            }
        }
    }

    return !caller_blocked_peer && !peer_blocked_caller;
}

Message DMHandler::handleDmSend(const Message& message, int fd) const {
    if (message.type != "DM_SEND") {
        return makeError("PROTOCOL_VIOLATION", "Expected DM_SEND message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "DM_SEND payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before sending a DM");
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "DM_SEND missing required field: user_id");
    }
    if (!message.payload.contains("content") || !message.payload["content"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "DM_SEND missing required field: content");
    }

    const std::string peer_id = message.payload["user_id"].get<std::string>();
    if (peer_id == session->user_id) {
        return makeError("PROTOCOL_VIOLATION", "Cannot DM yourself");
    }

    const std::string content = message.payload["content"].get<std::string>();
    if (content.empty()) {
        return makeError("MALFORMED_MESSAGE", "DM_SEND content must not be empty");
    }
    if (content.length() > 500) {
        return makeError("MALFORMED_MESSAGE", "DM_SEND content must be <= 500 characters");
    }

    if (!session_manager_) {
        return makeError("INTERNAL_ERROR", "DM context unavailable");
    }

    // §3.2: no separate "target exists" check — a nonexistent peer_id
    // trivially fails both the friend and shared-guild checks below, so
    // it already yields DM_NOT_PERMITTED with no dedicated existence
    // lookup (and no extra live call on this path) needed. This is a
    // deliberate simplification versus a literal reading of §3.5's
    // validation list: adding a mandatory existence check here would mean
    // every single DM_SEND pays a live round trip even when the peer is
    // online and everything else is already in-memory, undermining the
    // exact hot-path cost concern §3.3 was resolved to avoid.
    if (!canSendDm(session, peer_id)) {
        return makeError("DM_NOT_PERMITTED", "Cannot message this user");
    }

    const int message_id = ++message_counter_;
    const std::time_t timestamp = std::time(nullptr);

    Message response;
    response.type = "DM_MESSAGE";
    response.scope = Scope::TARGETED;
    std::vector<int> target_fds = session_manager_->getFdsForUser(session->user_id);
    const std::vector<int> peer_fds = session_manager_->getFdsForUser(peer_id);
    target_fds.insert(target_fds.end(), peer_fds.begin(), peer_fds.end());
    response.target_fds = target_fds;
    response.payload = nlohmann::json{{"type", "DM_MESSAGE"},
                                      {"message_id", message_id},
                                      {"timestamp", static_cast<long>(timestamp)},
                                      {"user_id", session->user_id},
                                      {"content", content}};

    // §3.4/§4.5's Option B, reused directly — fire-and-forget, enqueue
    // after building the response, never block on it.
    if (message_worker_) {
        message_worker_->enqueue({std::nullopt, peer_id, session->user_id, content, message_id});
    }

    return response;
}

Message DMHandler::handleFetchHistory(const Message& message, int fd) const {
    if (message.type != "FETCH_HISTORY") {
        return makeError("PROTOCOL_VIOLATION", "Expected FETCH_HISTORY message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "FETCH_HISTORY payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before fetching history");
    }

    if (!message.payload.contains("peer_id") || !message.payload["peer_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "FETCH_HISTORY peer_id must be a string");
    }
    const std::string peer_id = message.payload["peer_id"].get<std::string>();

    // §3.3's resolution: history reads never depend on canSendDm being
    // currently true, only on participancy — which is automatic here,
    // since the conversation is always resolved from (session->user_id,
    // peer_id), never a caller-supplied conversation id.

    std::optional<int> before_seq;
    if (message.payload.contains("before_seq") && !message.payload["before_seq"].is_null()) {
        if (!message.payload["before_seq"].is_number_integer()) {
            return makeError("MALFORMED_MESSAGE", "FETCH_HISTORY before_seq must be an integer or null");
        }
        before_seq = message.payload["before_seq"].get<int>();
    }

    int limit = kDefaultHistoryLimit;
    if (message.payload.contains("limit") && !message.payload["limit"].is_null()) {
        if (!message.payload["limit"].is_number_integer()) {
            return makeError("MALFORMED_MESSAGE", "FETCH_HISTORY limit must be an integer");
        }
        limit = message.payload["limit"].get<int>();
        if (limit < 1 || limit > kMaxHistoryLimit) {
            return makeError("MALFORMED_MESSAGE", "FETCH_HISTORY limit must be between 1 and " +
                                                       std::to_string(kMaxHistoryLimit));
        }
    }

    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "History unavailable");
    }

    const std::optional<http::HistoryPage> page =
        internal_api_client_->fetchMessages(std::nullopt, peer_id, session->user_id, before_seq, limit);
    if (!page) {
        return makeError("INTERNAL_ERROR", "Failed to fetch message history");
    }

    nlohmann::json messages_json = nlohmann::json::array();
    for (const auto& m : page->messages) {
        messages_json.push_back(nlohmann::json{{"message_id", m.message_id},
                                               {"timestamp", m.timestamp},
                                               {"user_id", m.user_id},
                                               {"username", m.username},
                                               {"content", m.content}});
    }

    Message response;
    response.type = "MESSAGE_HISTORY";
    response.payload = nlohmann::json{{"type", "MESSAGE_HISTORY"},
                                      {"channel_id", nullptr},
                                      {"peer_id", peer_id},
                                      {"messages", messages_json},
                                      {"has_more", page->has_more}};
    return response;
}

Message DMHandler::handleListDmConversations(const Message& message, int fd) const {
    if (message.type != "LIST_DM_CONVERSATIONS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_DM_CONVERSATIONS message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_DM_CONVERSATIONS payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing DM conversations");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "DM context unavailable");
    }

    const std::optional<std::vector<http::WireDmConversation>> conversations =
        internal_api_client_->fetchDmConversations(session->user_id);
    if (!conversations) {
        return makeError("INTERNAL_ERROR", "Failed to fetch DM conversations");
    }

    nlohmann::json conversations_json = nlohmann::json::array();
    for (const auto& c : *conversations) {
        conversations_json.push_back(nlohmann::json{
            {"peer_id", c.peer_id},
            {"last_message_at", c.last_message_at.has_value() ? nlohmann::json(*c.last_message_at) : nullptr}});
    }

    Message response;
    response.type = "DM_CONVERSATION_LIST";
    response.payload = nlohmann::json{{"type", "DM_CONVERSATION_LIST"}, {"conversations", conversations_json}};
    return response;
}

} // namespace protocol
