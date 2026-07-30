import { Logo } from "./Logo";
import { SignalIndicator } from "./SignalIndicator";

interface FeatureCardProps {
  title: string;
  description: string;
}

function FeatureCard({ title, description }: FeatureCardProps) {
  return (
    <div className="rounded-lg border border-slate/20 bg-surface/60 p-5">
      <h3 className="font-mono text-sm font-semibold text-teal">{title}</h3>
      <p className="mt-2 font-sans text-sm text-ivory/60">{description}</p>
    </div>
  );
}

// Shown when useGatewayConnection's status is "unauthenticated" — the
// public landing/home page. Structure is generic-SaaS (nav, hero, feature
// grid), not modeled on any particular product's actual layout; copy is
// original and grounded in what CIG Nexus actually is (shared/protocol/
// README.md, docs/auth/discord-design.md), not borrowed from Discord.
// Links to the existing /api/auth/discord/login route; no new backend or
// protocol code.
export function LandingView() {
  return (
    <div className="min-h-screen bg-ink text-ivory">
      <nav className="flex items-center justify-between border-b border-slate/20 px-6 py-4">
        <Logo withWordmark />
        <div className="flex items-center gap-6">
          <SignalIndicator status="unauthenticated" />
          <a href="/api/auth/discord/login" className="font-mono text-sm text-ivory/70 hover:text-ivory">
            Sign in
          </a>
        </div>
      </nav>

      <section className="mx-auto flex max-w-3xl flex-col items-center gap-6 px-6 py-24 text-center">
        <h1 className="font-mono text-4xl font-bold tracking-tight text-ivory sm:text-5xl">
          Chat, organized around <span className="text-teal">guilds</span> and{" "}
          <span className="text-violet">channels</span>.
        </h1>
        <p className="max-w-xl font-sans text-base text-ivory/60">
          CIG Nexus is a real-time chat platform built on its own protocol — a lightweight
          WebSocket-to-TCP bridge that carries every guild, channel, and message as a typed frame,
          live.
        </p>

        <div className="mt-4 flex flex-col items-center gap-3 sm:flex-row">
          <a
            href="/api/auth/discord/login"
            className="cursor-pointer rounded-md bg-teal px-5 py-2.5 font-mono text-sm font-semibold text-ink transition-colors hover:bg-teal/90"
          >
            Continue with Discord
          </a>

          <div className="flex items-center gap-2">
            <button
              disabled
              className="cursor-not-allowed rounded-md border border-slate/40 bg-surface px-5 py-2.5 font-mono text-sm font-semibold text-ivory/30"
            >
              Download for Desktop
            </button>
            <span className="font-mono text-xs text-ivory/30">Coming soon</span>
          </div>
        </div>
      </section>

      <section className="mx-auto grid max-w-4xl gap-4 px-6 pb-24 sm:grid-cols-3">
        <FeatureCard
          title="Guilds & Channels"
          description="Organize conversations into guilds, each holding its own set of text channels — create, join, and switch between them freely."
        />
        <FeatureCard
          title="A Protocol You Can Trace"
          description="No hidden magic. Every action — HELLO, IDENTIFY, CHAT_MESSAGE — is a typed JSON frame you could read straight off the wire."
        />
        <FeatureCard
          title="Discord-Backed Sessions"
          description="Sign in once with Discord. A short-lived signed token handles everything else, from the browser to the server."
        />
      </section>
    </div>
  );
}
