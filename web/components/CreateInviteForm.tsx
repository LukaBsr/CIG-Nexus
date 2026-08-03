import { useState } from "react";

import { Button } from "./Button";
import { TextInput } from "./TextInput";

interface CreateInviteFormProps {
  onSubmit: (maxUses: number | null, expiresInSeconds: number | null) => void;
}

// docs/guilds/social-presence-design.md §1.4/§6 step 6: exposes max_uses and
// expires_in_seconds instead of a client hardcoding null/null — both stay
// optional, matching §1.6's reusable/never-expiring-by-default
// recommendation. expires_in_seconds is entered here as days for a more
// natural input and converted at submit time.
export function CreateInviteForm({ onSubmit }: CreateInviteFormProps) {
  const [maxUses, setMaxUses] = useState("");
  const [expiresInDays, setExpiresInDays] = useState("");

  const handleSubmit = () => {
    const parsedMaxUses = maxUses.trim() ? Number(maxUses) : null;
    const parsedExpiresInSeconds = expiresInDays.trim() ? Number(expiresInDays) * 86400 : null;
    onSubmit(parsedMaxUses, parsedExpiresInSeconds);
    setMaxUses("");
    setExpiresInDays("");
  };

  return (
    <div className="flex flex-col gap-2">
      <TextInput
        type="number"
        min={1}
        value={maxUses}
        onChange={(e) => setMaxUses(e.target.value)}
        placeholder="Max uses (blank = unlimited)"
      />
      <TextInput
        type="number"
        min={1}
        value={expiresInDays}
        onChange={(e) => setExpiresInDays(e.target.value)}
        placeholder="Expires in days (blank = never)"
      />
      <Button onClick={handleSubmit} className="w-full">
        Create Invite
      </Button>
    </div>
  );
}
