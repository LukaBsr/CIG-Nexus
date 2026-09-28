#include "Server.hpp"
#include "protocol/MessageBuilders.hpp"
#include "util/DebugFlags.hpp"

#include <string>
#include <vector>

protocol::Message Server::makePresenceUpdate(const std::string& user_id, bool online) const {
    protocol::Message presence;
    presence.type = "PRESENCE_UPDATE";
    presence.scope = protocol::Scope::BROADCAST;
    presence.payload = nlohmann::json{{"type", "PRESENCE_UPDATE"},
                                      {"user_id", user_id},
                                      {"status", online ? "online" : "offline"}};
    return presence;
}

std::vector<int>
Server::computePresenceExclusionFds(const std::vector<std::string>& blocked_user_ids) const {
    std::vector<int> excluded;
    for (const auto& blocked_user_id : blocked_user_ids) {
        const std::vector<int> fds = session_manager_.getFdsForUser(blocked_user_id);
        excluded.insert(excluded.end(), fds.begin(), fds.end());
    }
    return excluded;
}

void Server::removeSessionTrackingPresence(int fd) {
    // INVARIANT: `blocked_user_ids` and `had_presence` below must be captured
    // BEFORE session_manager_.removeSession(fd) — that call erases this
    // connection's Session, after which neither the block list (needed for
    // the offline broadcast's exclusion set) nor session_context_ready
    // (needed to know whether presence was ever incremented) can be read.
    // Never move a read of `session` below removeSession().
    const session::Session* session = session_manager_.getSession(fd);
    const std::string user_id = session ? session->user_id : std::string();
    // docs/social/friends-dms-design.md §2.5: captured *before*
    // removeSession() below erases this connection's Session — the
    // exclusion set still needs to reflect who this user had blocked.
    const std::vector<std::string> blocked_user_ids =
        session ? session->blocked_user_ids : std::vector<std::string>();
    // IDENTIFY hardening (B2): a session whose async hydration
    // (processHydrationResults()) never completed never incremented
    // presence either — decrementing here anyway wouldn't just be a
    // harmless no-op, it would wrongly erode a *different*,
    // already-online connection's real count for this same user_id (two
    // tabs, one still pending, one fully online: the pending tab
    // disconnecting must not make the online tab look offline). Checking
    // session_context_ready, not just "did this fd ever get a user_id,"
    // is what makes that distinction.
    const bool had_presence = session && session->session_context_ready;

    util::logPresenceDebug("removeSessionTrackingPresence fd=" + std::to_string(fd) +
                           " user_id=" + (user_id.empty() ? "(never identified)" : user_id) +
                           (had_presence ? "" : " (no matching presence increment)"));

    session_manager_.removeSession(fd);

    if (had_presence && session_manager_.decrementPresence(user_id)) {
        broadcastExcluding(makePresenceUpdate(user_id, false),
                           computePresenceExclusionFds(blocked_user_ids));
    }
}

void Server::processHydrationResults() {
    if (!hydration_worker_) {
        return;
    }

    // INVARIANT: `conn_it` is held across removeSessionTrackingPresence() in
    // the failure branch below and erased only afterwards, by this function.
    // That is safe solely because removeSessionTrackingPresence() (and the
    // broadcasts it triggers) never insert into or erase from connections_ —
    // they only read it. Do not add connections_ mutation to that path
    // without revisiting this loop.
    for (const auto& result : hydration_worker_->drainResults()) {
        const auto conn_it = connections_.find(result.fd);
        if (conn_it == connections_.end()) {
            continue; // Connection already gone by the time this landed.
        }

        session::Session* session = session_manager_.getSession(result.fd);
        if (!session || session->app_session_id != result.app_session_id) {
            // fd reused by an unrelated connection/IDENTIFY since this job
            // was enqueued (docs/known-issues.md's fd-reuse-race idea) —
            // matching on app_session_id, not just fd, catches that case
            // and drops the stale result instead of misapplying it to the
            // wrong session.
            continue;
        }

        if (result.succeeded) {
            for (const auto& block : result.context.blocks) {
                session->blocked_user_ids.push_back(block.user_id);
            }
            for (const auto& f : result.context.friends) {
                session->friend_ids.push_back(f.user_id);
            }
            session->display_name = result.context.profile.display_name;
            session->avatar_url = result.context.profile.avatar_url;
            session->session_context_ready = true;

            util::logPresenceDebug("hydration complete fd=" + std::to_string(result.fd) +
                                   " user_id=" + session->user_id);
            if (session_manager_.incrementPresence(session->user_id)) {
                broadcastExcluding(makePresenceUpdate(session->user_id, true),
                                   computePresenceExclusionFds(session->blocked_user_ids));
            }
        } else {
            // shared/protocol/README.md's Asynchronous IDENTIFY Hydration
            // section: fail closed, not open — an unenforced block list is
            // worse than a dropped connection the client must reconnect
            // for.
            util::logPresenceDebug("hydration failed fd=" + std::to_string(result.fd) +
                                   " user_id=" + session->user_id + " -- disconnecting");

            sendMessage(result.fd, protocol::make_error_message(
                                       "SESSION_CONTEXT_UNAVAILABLE",
                                       "Could not load account context in time; please reconnect"));

            removeSessionTrackingPresence(result.fd); // invalidates `session` above
            connections_.erase(conn_it);
        }
    }
}
