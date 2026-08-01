CREATE TYPE "public"."guild_visibility" AS ENUM('open', 'application', 'private');--> statement-breakpoint
CREATE TABLE "guild_invites" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"guild_id" uuid NOT NULL,
	"code" text NOT NULL,
	"created_by" uuid NOT NULL,
	"max_uses" integer,
	"use_count" integer DEFAULT 0 NOT NULL,
	"expires_at" timestamp with time zone,
	"revoked_at" timestamp with time zone,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "guild_invites_code_unique" UNIQUE("code"),
	CONSTRAINT "guild_invites_max_uses_positive" CHECK ("guild_invites"."max_uses" IS NULL OR "guild_invites"."max_uses" > 0),
	CONSTRAINT "guild_invites_use_count_non_negative" CHECK ("guild_invites"."use_count" >= 0)
);
--> statement-breakpoint
CREATE TABLE "guild_join_requests" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"guild_id" uuid NOT NULL,
	"user_id" uuid NOT NULL,
	"requested_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "guild_join_requests_guild_id_user_id_key" UNIQUE("guild_id","user_id")
);
--> statement-breakpoint
ALTER TABLE "guilds" ADD COLUMN "visibility" "guild_visibility" DEFAULT 'open' NOT NULL;--> statement-breakpoint
ALTER TABLE "guild_invites" ADD CONSTRAINT "guild_invites_guild_id_guilds_id_fk" FOREIGN KEY ("guild_id") REFERENCES "public"."guilds"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "guild_invites" ADD CONSTRAINT "guild_invites_created_by_users_id_fk" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "guild_join_requests" ADD CONSTRAINT "guild_join_requests_guild_id_guilds_id_fk" FOREIGN KEY ("guild_id") REFERENCES "public"."guilds"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "guild_join_requests" ADD CONSTRAINT "guild_join_requests_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "idx_guild_invites_guild_id" ON "guild_invites" USING btree ("guild_id");--> statement-breakpoint
CREATE INDEX "idx_guild_join_requests_guild_id" ON "guild_join_requests" USING btree ("guild_id");