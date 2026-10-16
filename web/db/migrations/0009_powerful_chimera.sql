CREATE TABLE "dm_read_state" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"user_id" uuid NOT NULL,
	"dm_conversation_id" uuid NOT NULL,
	"last_read_seq" bigint DEFAULT 0 NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "dm_read_state_user_id_dm_conversation_id_key" UNIQUE("user_id","dm_conversation_id"),
	CONSTRAINT "dm_read_state_last_read_seq_check" CHECK ("dm_read_state"."last_read_seq" >= 0)
);
--> statement-breakpoint
ALTER TABLE "dm_read_state" ADD CONSTRAINT "dm_read_state_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "dm_read_state" ADD CONSTRAINT "dm_read_state_dm_conversation_id_dm_conversations_id_fk" FOREIGN KEY ("dm_conversation_id") REFERENCES "public"."dm_conversations"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
-- Backfill: a missing dm_read_state row means marker 0, which would make
-- every message in every existing conversation show as unread the moment
-- this ships. Give both participants of every existing dm_conversations
-- row a row at that conversation's current max seq (0 for an empty
-- conversation, via the COALESCE) — "already caught up," not "never
-- read." A conversation created after this migration runs gets no row
-- until its first MARK_DM_READ, which is correct: nothing has been read
-- yet. ON CONFLICT DO NOTHING is redundant in this same migration (the
-- table is brand new above) but makes a partial re-run safe.
INSERT INTO "dm_read_state" ("user_id", "dm_conversation_id", "last_read_seq")
SELECT participant.user_id, dc.id, COALESCE(maxseq.max_seq, 0)
FROM "dm_conversations" dc
CROSS JOIN LATERAL (VALUES (dc.user_id_a), (dc.user_id_b)) AS participant(user_id)
LEFT JOIN LATERAL (
	SELECT MAX(m.seq) AS max_seq FROM "messages" m WHERE m.dm_conversation_id = dc.id
) AS maxseq ON true
ON CONFLICT ("user_id", "dm_conversation_id") DO NOTHING;