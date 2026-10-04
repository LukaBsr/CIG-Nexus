import Link from "next/link";

import { Logo } from "@/components/Logo";
import { SignalIndicator } from "@/components/SignalIndicator";

// Landed on by /api/auth/discord/callback's redirectToError() — a
// deliberately generic outcome (design doc §4 step 8: never reveal which
// specific check failed), so the copy here has to stay generic too rather
// than guessing at a reason. No new components: the nav mirrors
// LandingView's, and the CTA reuses the same primary-button treatment
// already established there.
export default function LoginErrorPage() {
  return (
    <div className="min-h-screen bg-page text-fg">
      <nav className="flex items-center justify-between border-b border-slate/20 px-6 py-4">
        <Logo withWordmark />
        <SignalIndicator status="unauthenticated" />
      </nav>

      <section className="mx-auto flex max-w-md flex-col items-center gap-4 px-6 py-32 text-center">
        <h1 className="font-mono text-2xl font-bold tracking-tight text-fg">
          Sign-in didn&apos;t go through
        </h1>
        <p className="font-sans text-sm text-muted">
          That Discord sign-in was cancelled or couldn&apos;t be completed. No harm done — you can
          try again.
        </p>

        <Link
          href="/"
          className="mt-4 cursor-pointer rounded-md bg-brand px-5 py-2.5 font-mono text-sm font-semibold text-page transition-colors hover:bg-brand/90"
        >
          Back to CIG Nexus
        </Link>
      </section>
    </div>
  );
}
