import type { ComponentType } from "react";

import { AppearanceSettings } from "@/components/settings/AppearanceSettings";

// docs/settings-appearance-design.md §1.2: the settings modal shell is
// fixed in place around this registry — adding a section (Account,
// Notifications, ...) later is one entry here plus one component, never a
// change to SettingsModal itself.
export interface SettingsSection {
  id: string;
  label: string;
  Component: ComponentType;
}

export const SETTINGS_SECTIONS: SettingsSection[] = [
  { id: "appearance", label: "Appearance", Component: AppearanceSettings }
];
