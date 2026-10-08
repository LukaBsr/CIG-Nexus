import { useId } from "react";

interface MarkProps {
  size: number;
  className?: string;
  // Omit for a decorative mark — the surrounding element already carries
  // the accessible name (GuildRail's button has its own aria-label).
  // Provide the same text next/image's alt used to carry, for a mark that
  // is itself the content (Logo.tsx).
  title?: string;
}

// Inline replacement for web/public/branding/mark-dark.svg — same 512
// geometry, same six shapes — but colored entirely through theme tokens
// via CSS variables, so it follows data-theme with no JS and no per-theme
// asset (docs/design/brand-guidelines.md §5.3 item 5 / §5.4, PR L1). The
// static SVG was always blue-teal-violet, regardless of the active theme.
// Gradient and filter ids use useId: several marks can be mounted on one
// page (the header and the rail both render one), and SVG ids are global
// to the document, so two instances sharing a literal id would mean the
// second one's gradient/filter silently wins for both.
export function Mark({ size, className = "", title }: MarkProps) {
  const uid = useId();
  const gradientId = `mark-shard-${uid}`;
  const glowId = `mark-glow-${uid}`;

  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 512 512"
      className={className}
      role={title ? "img" : undefined}
      aria-label={title}
      aria-hidden={title ? undefined : "true"}
    >
      {title && <title>{title}</title>}
      <defs>
        <linearGradient id={gradientId} x1="0%" y1="0%" x2="0%" y2="100%">
          <stop offset="0%" stopColor="var(--color-brand)" />
          <stop offset="100%" stopColor="var(--color-brand-2)" />
        </linearGradient>
        <filter id={glowId} x="-50%" y="-50%" width="200%" height="200%">
          <feGaussianBlur stdDeviation="5" result="b" />
          <feMerge>
            <feMergeNode in="b" />
            <feMergeNode in="SourceGraphic" />
          </feMerge>
        </filter>
      </defs>

      {/* Crystal shard outline (line art, no fill): the brand -> brand-2
          gradient. cig-mark-glow is globals.css's data-mode="light" hook —
          filter is always set here, so the default (dark) needs no rule
          of its own; light themes turn it off. */}
      <g className="cig-mark-glow" filter={`url(#${glowId})`}>
        <polygon
          points="256,72 336,168 306,352 256,432 206,352 176,168"
          fill="none"
          stroke={`url(#${gradientId})`}
          strokeWidth="7"
          strokeLinejoin="round"
        />
        <polyline
          points="176,168 256,196 336,168"
          fill="none"
          stroke={`url(#${gradientId})`}
          strokeWidth="3"
          opacity="0.75"
        />
        <polyline
          points="206,352 256,320 306,352"
          fill="none"
          stroke={`url(#${gradientId})`}
          strokeWidth="3"
          opacity="0.75"
        />
        <line
          x1="256"
          y1="196"
          x2="256"
          y2="320"
          stroke={`url(#${gradientId})`}
          strokeWidth="3"
          opacity="0.55"
        />
      </g>

      {/* Three-hop topology through the core: browser (top, muted) /
          gateway (diamond, fg) / server (bottom, brand) */}
      <line
        x1="256"
        y1="30"
        x2="256"
        y2="196"
        stroke="var(--color-muted)"
        strokeWidth="5"
        strokeDasharray="3 11"
        strokeLinecap="round"
      />
      <circle cx="256" cy="30" r="15" fill="var(--color-muted)" />
      <rect
        x="244"
        y="246"
        width="24"
        height="24"
        rx="5"
        fill="var(--color-fg)"
        transform="rotate(45 256 258)"
      />
      <line
        x1="256"
        y1="320"
        x2="256"
        y2="462"
        stroke="var(--color-brand)"
        strokeWidth="6"
        strokeLinecap="round"
      />
      <circle cx="256" cy="462" r="26" stroke="var(--color-brand)" strokeWidth="3" fill="none" opacity="0.4" />
      <circle cx="256" cy="462" r="16" fill="var(--color-brand)" />
    </svg>
  );
}
