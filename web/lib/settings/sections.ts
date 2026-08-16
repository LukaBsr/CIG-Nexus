import type { ComponentType } from "react";

import { AppearanceSettings } from "@/components/settings/AppearanceSettings";
import { BlockedUsersSettings } from "@/components/settings/BlockedUsersSettings";
import { ProfileSettings } from "@/components/settings/ProfileSettings";
import type { Block } from "@/lib/types";

// docs/settings/appearance-design.md §1.2: the settings modal shell is
// fixed in place around this registry — adding a section (Account,
// Notifications, ...) later is one entry here plus one component, never a
// change to SettingsModal itself.
//
// docs/social/friends-dms-design.md §4.3 is the second entry this was
// designed to accommodate. It's the first section that actually needs
// data (the caller's own user_id) — every Component in the registry now
// takes SettingsSectionProps rather than no props at all, even ones (like
// AppearanceSettings) that don't use it, so the shell can pass the same
// props to whichever section is active without knowing which ones need
// what.
//
// blockedUsers/onUnblock (§2, added for the Blocked Users section) are
// optional for the same reason: unlike Profile's REST-fetched data, block
// state lives only in useGatewayConnection's WebSocket-sourced state, so
// it has to be threaded down from the shell rather than fetched
// independently the way ProfileSettings fetches its own data.
export interface SettingsSectionProps {
  userId: string | null;
  blockedUsers?: Block[];
  onUnblock?: (userId: string) => void;
}

export interface SettingsSection {
  id: string;
  label: string;
  Component: ComponentType<SettingsSectionProps>;
}

export const SETTINGS_SECTIONS: SettingsSection[] = [
  { id: "appearance", label: "Appearance", Component: AppearanceSettings },
  { id: "profile", label: "Profile", Component: ProfileSettings },
  { id: "blocked", label: "Blocked Users", Component: BlockedUsersSettings }
];
