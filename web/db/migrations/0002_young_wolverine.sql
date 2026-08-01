ALTER TABLE "guild_memberships" DROP CONSTRAINT "guild_memberships_role_check";--> statement-breakpoint
ALTER TABLE "guilds" ADD COLUMN "role_theme" text DEFAULT 'pirate' NOT NULL;--> statement-breakpoint
ALTER TABLE "guild_memberships" ADD COLUMN "role_rank" integer DEFAULT 0 NOT NULL;--> statement-breakpoint
UPDATE "guild_memberships" SET "role_rank" = 2 WHERE "role" = 'owner';--> statement-breakpoint
ALTER TABLE "guild_memberships" DROP COLUMN "role";--> statement-breakpoint
ALTER TABLE "guild_memberships" ADD CONSTRAINT "guild_memberships_role_rank_check" CHECK ("guild_memberships"."role_rank" >= 0);