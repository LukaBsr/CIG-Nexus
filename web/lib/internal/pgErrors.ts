// Postgres error codes for constraint failures we want to surface as 400s
// rather than 500s: unique_violation, foreign_key_violation,
// check_violation, not_null_violation, invalid_text_representation (e.g. a
// malformed UUID).
const CONSTRAINT_VIOLATION_CODES = new Set(["23505", "23503", "23514", "23502", "22P02"]);

export function isConstraintViolation(err: unknown): boolean {
  const cause = (err as { cause?: unknown })?.cause ?? err;
  const code = (cause as { code?: string } | undefined)?.code;
  return code !== undefined && CONSTRAINT_VIOLATION_CODES.has(code);
}
