import type { ComponentType } from "react";

import { AppearanceSettings } from "@/components/settings/AppearanceSettings";
import { ProfileSettings } from "@/components/settings/ProfileSettings";

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
export interface SettingsSectionProps {
  userId: string | null;
}

export interface SettingsSection {
  id: string;
  label: string;
  Component: ComponentType<SettingsSectionProps>;
}

export const SETTINGS_SECTIONS: SettingsSection[] = [
  { id: "appearance", label: "Appearance", Component: AppearanceSettings },
  { id: "profile", label: "Profile", Component: ProfileSettings }
];
