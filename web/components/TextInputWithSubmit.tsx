import type { KeyboardEvent } from "react";

import { Button } from "./Button";
import { TextInput } from "./TextInput";

interface TextInputWithSubmitProps {
  value: string;
  onChange: (value: string) => void;
  onSubmit: () => void;
  placeholder?: string;
  submitLabel: string;
  disabled?: boolean;
}

// The chat input, new-guild-name input, new-channel-name input, and
// channel-message input all repeat the same text-input-plus-submit-button
// pair (docs/frontend-rebuild-plan.md, extraction target 2). Deliberately
// has no outer margin of its own — call sites vary in the spacing they
// need around this pair, so that stays the caller's responsibility.
export function TextInputWithSubmit({
  value,
  onChange,
  onSubmit,
  placeholder,
  submitLabel,
  disabled = false
}: TextInputWithSubmitProps) {
  const handleKeyDown = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "Enter") {
      onSubmit();
    }
  };

  return (
    <div className="flex gap-2">
      <TextInput
        value={value}
        onChange={(e) => onChange(e.target.value)}
        onKeyDown={handleKeyDown}
        placeholder={placeholder}
      />
      <Button onClick={onSubmit} disabled={disabled}>
        {submitLabel}
      </Button>
    </div>
  );
}
