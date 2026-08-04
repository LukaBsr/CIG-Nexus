import { relations } from "drizzle-orm";

import { channels } from "./channels";
import { dmConversations } from "./dmConversations";
import { friendRequests } from "./friendRequests";
import { friendships } from "./friendships";
import { guildInvites } from "./guildInvites";
import { guildJoinRequests } from "./guildJoinRequests";
import { guildMemberships } from "./guildMemberships";
import { guilds } from "./guilds";
import { messages } from "./messages";
import { sessions } from "./sessions";
import { userBlocks } from "./userBlocks";
import { users } from "./users";

export const usersRelations = relations(users, ({ many }) => ({
  sessions: many(sessions),
  ownedGuilds: many(guilds),
  memberships: many(guildMemberships),
  messages: many(messages),
  createdInvites: many(guildInvites),
  joinRequests: many(guildJoinRequests),
  sentFriendRequests: many(friendRequests, { relationName: "friendRequestsAsRequester" }),
  receivedFriendRequests: many(friendRequests, { relationName: "friendRequestsAsRecipient" }),
  friendshipsAsUserA: many(friendships, { relationName: "friendshipsAsUserA" }),
  friendshipsAsUserB: many(friendships, { relationName: "friendshipsAsUserB" }),
  blocksMade: many(userBlocks, { relationName: "userBlocksAsBlocker" }),
  blocksReceived: many(userBlocks, { relationName: "userBlocksAsBlocked" }),
  dmConversationsAsUserA: many(dmConversations, { relationName: "dmConversationsAsUserA" }),
  dmConversationsAsUserB: many(dmConversations, { relationName: "dmConversationsAsUserB" })
}));

export const dmConversationsRelations = relations(dmConversations, ({ one, many }) => ({
  userA: one(users, {
    fields: [dmConversations.userIdA],
    references: [users.id],
    relationName: "dmConversationsAsUserA"
  }),
  userB: one(users, {
    fields: [dmConversations.userIdB],
    references: [users.id],
    relationName: "dmConversationsAsUserB"
  }),
  messages: many(messages)
}));

export const userBlocksRelations = relations(userBlocks, ({ one }) => ({
  blocker: one(users, {
    fields: [userBlocks.blockerId],
    references: [users.id],
    relationName: "userBlocksAsBlocker"
  }),
  blocked: one(users, {
    fields: [userBlocks.blockedId],
    references: [users.id],
    relationName: "userBlocksAsBlocked"
  })
}));

// docs/social/friends-dms-design.md §1.2. Two FKs to users on each table
// need relationName to disambiguate which is which — plain `one(users,
// ...)` twice on the same table is ambiguous to drizzle without it.
export const friendRequestsRelations = relations(friendRequests, ({ one }) => ({
  requester: one(users, {
    fields: [friendRequests.requesterId],
    references: [users.id],
    relationName: "friendRequestsAsRequester"
  }),
  recipient: one(users, {
    fields: [friendRequests.recipientId],
    references: [users.id],
    relationName: "friendRequestsAsRecipient"
  })
}));

export const friendshipsRelations = relations(friendships, ({ one }) => ({
  userA: one(users, {
    fields: [friendships.userIdA],
    references: [users.id],
    relationName: "friendshipsAsUserA"
  }),
  userB: one(users, {
    fields: [friendships.userIdB],
    references: [users.id],
    relationName: "friendshipsAsUserB"
  })
}));

export const sessionsRelations = relations(sessions, ({ one }) => ({
  user: one(users, { fields: [sessions.userId], references: [users.id] })
}));

export const guildsRelations = relations(guilds, ({ one, many }) => ({
  owner: one(users, { fields: [guilds.ownerId], references: [users.id] }),
  memberships: many(guildMemberships),
  channels: many(channels),
  invites: many(guildInvites),
  joinRequests: many(guildJoinRequests)
}));

export const guildInvitesRelations = relations(guildInvites, ({ one }) => ({
  guild: one(guilds, { fields: [guildInvites.guildId], references: [guilds.id] }),
  creator: one(users, { fields: [guildInvites.createdBy], references: [users.id] })
}));

export const guildJoinRequestsRelations = relations(guildJoinRequests, ({ one }) => ({
  guild: one(guilds, { fields: [guildJoinRequests.guildId], references: [guilds.id] }),
  user: one(users, { fields: [guildJoinRequests.userId], references: [users.id] })
}));

export const guildMembershipsRelations = relations(guildMemberships, ({ one }) => ({
  guild: one(guilds, { fields: [guildMemberships.guildId], references: [guilds.id] }),
  user: one(users, { fields: [guildMemberships.userId], references: [users.id] })
}));

export const channelsRelations = relations(channels, ({ one, many }) => ({
  guild: one(guilds, { fields: [channels.guildId], references: [guilds.id] }),
  messages: many(messages)
}));

export const messagesRelations = relations(messages, ({ one }) => ({
  channel: one(channels, { fields: [messages.channelId], references: [channels.id] }),
  dmConversation: one(dmConversations, { fields: [messages.dmConversationId], references: [dmConversations.id] }),
  user: one(users, { fields: [messages.userId], references: [users.id] })
}));
