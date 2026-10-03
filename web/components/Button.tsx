import type { ButtonHTMLAttributes } from "react";

type ButtonVariant = "primary" | "secondary";

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
}

const VARIANT_CLASSES: Record<ButtonVariant, string> = {
  primary: "bg-brand text-page hover:bg-brand/90",
  secondary: "border border-slate bg-surface text-fg hover:border-brand/60"
};

export function Button({ variant = "primary", className = "", disabled, ...props }: ButtonProps) {
  const stateClasses = disabled
    ? "cursor-not-allowed border border-slate/40 bg-surface text-fg/30"
    : `cursor-pointer ${VARIANT_CLASSES[variant]}`;

  return (
    <button
      className={`rounded-md px-4 py-2 font-mono text-sm font-medium transition-colors ${stateClasses} ${className}`}
      disabled={disabled}
      {...props}
    />
  );
}
