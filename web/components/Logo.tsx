import Image from "next/image";

interface LogoProps {
  size?: "sm" | "lg";
  withWordmark?: boolean;
  className?: string;
}

const ICON_PX: Record<"sm" | "lg", number> = { sm: 28, lg: 64 };

// The crystal mark, from web/public/branding/icon.svg. The wordmark is
// re-set here as live text (matching the lockup's monospace/tracked
// treatment) rather than embedding lockup.svg directly — lockup.svg's own
// canvas has a lot of trailing whitespace baked in, which live text avoids
// while staying pixel-faithful to the same "CIG" / "NEXUS" split.
export function Logo({ size = "sm", withWordmark = false, className = "" }: LogoProps) {
  const px = ICON_PX[size];

  return (
    <div className={`flex items-center gap-3 ${className}`}>
      <Image src="/branding/icon.svg" width={px} height={px} alt="CIG Nexus" className="shrink-0" />
      {withWordmark && (
        <span
          className={`font-mono font-bold tracking-[0.2em] ${size === "lg" ? "text-3xl" : "text-sm"}`}
        >
          <span className="text-ivory">CIG</span> <span className="text-teal">NEXUS</span>
        </span>
      )}
    </div>
  );
}
