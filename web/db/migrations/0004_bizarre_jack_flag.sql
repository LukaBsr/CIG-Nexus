ALTER TABLE "users" ADD COLUMN "theme" text;--> statement-breakpoint
ALTER TABLE "users" ADD COLUMN "theme_sync_enabled" boolean DEFAULT false NOT NULL;