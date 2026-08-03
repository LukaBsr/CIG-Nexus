"use client";

import { useEffect, useRef } from "react";

// Shared dismiss behavior for the small anchored popovers (invite creation,
// join-request inbox) — click outside or Escape closes it. The returned ref
// should wrap both the trigger button and the panel together, so the
// trigger's own click (which toggles open) isn't also seen as "outside."
export function useDismissablePopover<T extends HTMLElement>(
  open: boolean,
  onClose: () => void
): React.RefObject<T | null> {
  const ref = useRef<T>(null);

  useEffect(() => {
    if (!open) {
      return;
    }

    const handlePointerDown = (e: PointerEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) {
        onClose();
      }
    };
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        onClose();
      }
    };

    document.addEventListener("pointerdown", handlePointerDown);
    document.addEventListener("keydown", handleKeyDown);
    return () => {
      document.removeEventListener("pointerdown", handlePointerDown);
      document.removeEventListener("keydown", handleKeyDown);
    };
  }, [open, onClose]);

  return ref;
}
