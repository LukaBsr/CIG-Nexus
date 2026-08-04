ALTER TABLE "users" ADD COLUMN "display_name" text;--> statement-breakpoint
ALTER TABLE "users" ADD COLUMN "bio" text;--> statement-breakpoint
ALTER TABLE "users" ADD COLUMN "status_message" text;--> statement-breakpoint
ALTER TABLE "users" ADD COLUMN "accent_color" text;--> statement-breakpoint
ALTER TABLE "users" ADD COLUMN "custom_avatar_path" text;--> statement-breakpoint
ALTER TABLE "users" ADD CONSTRAINT "users_display_name_length" CHECK ("users"."display_name" IS NULL OR char_length("users"."display_name") BETWEEN 1 AND 32);--> statement-breakpoint
ALTER TABLE "users" ADD CONSTRAINT "users_bio_length" CHECK ("users"."bio" IS NULL OR char_length("users"."bio") <= 300);--> statement-breakpoint
ALTER TABLE "users" ADD CONSTRAINT "users_status_message_length" CHECK ("users"."status_message" IS NULL OR char_length("users"."status_message") <= 100);--> statement-breakpoint
ALTER TABLE "users" ADD CONSTRAINT "users_accent_color_format" CHECK ("users"."accent_color" IS NULL OR "users"."accent_color" ~ '^#[0-9a-fA-F]{6}$');