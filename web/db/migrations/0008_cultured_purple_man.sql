CREATE TABLE "dm_conversations" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"user_id_a" uuid NOT NULL,
	"user_id_b" uuid NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "dm_conversations_user_id_a_user_id_b_key" UNIQUE("user_id_a","user_id_b"),
	CONSTRAINT "dm_conversations_ordered_pair" CHECK ("dm_conversations"."user_id_a" < "dm_conversations"."user_id_b")
);
--> statement-breakpoint
DROP INDEX "idx_messages_lobby_seq";--> statement-breakpoint
ALTER TABLE "messages" ADD COLUMN "dm_conversation_id" uuid;--> statement-breakpoint
ALTER TABLE "dm_conversations" ADD CONSTRAINT "dm_conversations_user_id_a_users_id_fk" FOREIGN KEY ("user_id_a") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "dm_conversations" ADD CONSTRAINT "dm_conversations_user_id_b_users_id_fk" FOREIGN KEY ("user_id_b") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "messages" ADD CONSTRAINT "messages_dm_conversation_id_dm_conversations_id_fk" FOREIGN KEY ("dm_conversation_id") REFERENCES "public"."dm_conversations"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
CREATE UNIQUE INDEX "idx_messages_dm_seq" ON "messages" USING btree ("seq") WHERE "messages"."dm_conversation_id" IS NOT NULL;--> statement-breakpoint
CREATE INDEX "idx_messages_dm_created" ON "messages" USING btree ("dm_conversation_id","seq");--> statement-breakpoint
CREATE UNIQUE INDEX "idx_messages_lobby_seq" ON "messages" USING btree ("seq") WHERE "messages"."channel_id" IS NULL AND "messages"."dm_conversation_id" IS NULL;--> statement-breakpoint
ALTER TABLE "messages" ADD CONSTRAINT "messages_at_most_one_scope" CHECK (NOT ("messages"."channel_id" IS NOT NULL AND "messages"."dm_conversation_id" IS NOT NULL));