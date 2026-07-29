import { relations } from "drizzle-orm";

import { channels } from "./channels";
import { guildMemberships } from "./guildMemberships";
import { guilds } from "./guilds";
import { sessions } from "./sessions";
import { users } from "./users";

export const usersRelations = relations(users, ({ many }) => ({
  sessions: many(sessions),
  ownedGuilds: many(guilds),
  memberships: many(guildMemberships)
}));

export const sessionsRelations = relations(sessions, ({ one }) => ({
  user: one(users, { fields: [sessions.userId], references: [users.id] })
}));

export const guildsRelations = relations(guilds, ({ one, many }) => ({
  owner: one(users, { fields: [guilds.ownerId], references: [users.id] }),
  memberships: many(guildMemberships),
  channels: many(channels)
}));

export const guildMembershipsRelations = relations(guildMemberships, ({ one }) => ({
  guild: one(guilds, { fields: [guildMemberships.guildId], references: [guilds.id] }),
  user: one(users, { fields: [guildMemberships.userId], references: [users.id] })
}));

export const channelsRelations = relations(channels, ({ one }) => ({
  guild: one(guilds, { fields: [channels.guildId], references: [guilds.id] })
}));
