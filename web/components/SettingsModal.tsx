"use client";

import { useEffect, useState } from "react";

import { SETTINGS_SECTIONS } from "@/lib/settings/sections";

interface SettingsModalProps {
  onClose: () => void;
}

// docs/settings-appearance-design.md §1.1: a centered overlay, not a
// TabGroup entry — settings is a transient surface on top of unchanged
// state underneath, not a peer view of Lobby/Guilds. §1.2's section rail
// is built as a list even with one entry today (Appearance) so a second
// section is additive, never a rearchitect.
export function SettingsModal({ onClose }: SettingsModalProps) {
  const [activeSectionId, setActiveSectionId] = useState(SETTINGS_SECTIONS[0].id);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        onClose();
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [onClose]);

  const activeSection =
    SETTINGS_SECTIONS.find((section) => section.id === activeSectionId) ?? SETTINGS_SECTIONS[0];
  const ActiveComponent = activeSection.Component;

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-ink/70 p-4"
      onClick={onClose}
    >
      <div
        className="flex w-full max-w-2xl overflow-hidden rounded-lg border border-slate/40 bg-surface shadow-xl"
        onClick={(e) => e.stopPropagation()}
      >
        <aside className="w-40 shrink-0 border-r border-slate/20 p-3">
          <h2 className="mb-3 px-2 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Settings
          </h2>
          <ul className="flex list-none flex-col gap-1 p-0">
            {SETTINGS_SECTIONS.map((section) => (
              <li key={section.id}>
                <button
                  onClick={() => setActiveSectionId(section.id)}
                  className={`w-full rounded-md px-3 py-1.5 text-left font-mono text-sm transition-colors ${
                    section.id === activeSectionId
                      ? "bg-teal/15 font-semibold text-teal"
                      : "text-ivory/60 hover:text-ivory"
                  }`}
                >
                  {section.label}
                </button>
              </li>
            ))}
          </ul>
        </aside>

        <div className="flex max-h-[70vh] flex-1 flex-col overflow-y-auto p-5">
          <div className="mb-4 flex shrink-0 items-center justify-between">
            <h3 className="font-mono text-sm font-semibold text-ivory">{activeSection.label}</h3>
            <button
              onClick={onClose}
              aria-label="Close settings"
              className="font-mono text-lg leading-none text-ivory/50 transition-colors hover:text-ivory"
            >
              &times;
            </button>
          </div>
          <ActiveComponent />
        </div>
      </div>
    </div>
  );
}
