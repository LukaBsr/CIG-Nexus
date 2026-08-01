import { relations } from "drizzle-orm";

import { channels } from "./channels";
import { guildInvites } from "./guildInvites";
import { guildJoinRequests } from "./guildJoinRequests";
import { guildMemberships } from "./guildMemberships";
import { guilds } from "./guilds";
import { messages } from "./messages";
import { sessions } from "./sessions";
import { users } from "./users";

export const usersRelations = relations(users, ({ many }) => ({
  sessions: many(sessions),
  ownedGuilds: many(guilds),
  memberships: many(guildMemberships),
  messages: many(messages),
  createdInvites: many(guildInvites),
  joinRequests: many(guildJoinRequests)
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
  user: one(users, { fields: [messages.userId], references: [users.id] })
}));
