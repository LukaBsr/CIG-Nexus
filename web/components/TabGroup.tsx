import type { ReactNode } from "react";

interface TabButtonProps {
  active: boolean;
  onClick: () => void;
  children: ReactNode;
}

export function TabButton({ active, onClick, children }: TabButtonProps) {
  return (
    <button
      onClick={onClick}
      className={`cursor-pointer rounded-md px-4 py-1.5 font-mono text-sm transition-colors ${
        active ? "bg-teal font-semibold text-ink" : "text-ivory/60 hover:text-ivory"
      }`}
    >
      {children}
    </button>
  );
}

interface TabGroupProps {
  children: ReactNode;
}

export function TabGroup({ children }: TabGroupProps) {
  return (
    <div className="inline-flex gap-1 rounded-lg border border-slate/40 bg-surface p-1">{children}</div>
  );
}
