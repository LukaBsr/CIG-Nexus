import type { InputHTMLAttributes } from "react";

type TextInputProps = InputHTMLAttributes<HTMLInputElement>;

export function TextInput({ className = "", ...props }: TextInputProps) {
  return (
    <input
      type="text"
      className={`min-w-0 flex-1 rounded-md border border-slate/50 bg-surface px-3 py-2 font-sans text-sm text-ivory outline-none transition-colors placeholder:text-ivory/30 focus:border-teal ${className}`}
      {...props}
    />
  );
}
