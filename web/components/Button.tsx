import type { ButtonHTMLAttributes } from "react";

type ButtonVariant = "primary" | "secondary";

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
}

const VARIANT_CLASSES: Record<ButtonVariant, string> = {
  primary: "bg-teal text-ink hover:bg-teal/90",
  secondary: "border border-slate bg-surface text-ivory hover:border-teal/60"
};

export function Button({ variant = "primary", className = "", disabled, ...props }: ButtonProps) {
  const stateClasses = disabled
    ? "cursor-not-allowed border border-slate/40 bg-surface text-ivory/30"
    : `cursor-pointer ${VARIANT_CLASSES[variant]}`;

  return (
    <button
      className={`rounded-md px-4 py-2 font-mono text-sm font-medium transition-colors ${stateClasses} ${className}`}
      disabled={disabled}
      {...props}
    />
  );
}
