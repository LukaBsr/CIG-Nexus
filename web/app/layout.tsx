import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import { cookies } from "next/headers";
import "./globals.css";

import { THEME_COOKIE } from "@/lib/appearance/cookie";
import { resolveThemeId } from "@/lib/appearance/themes";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

export const metadata: Metadata = {
  title: "CIG Nexus",
  description: "CIG Nexus — real-time chat with guilds and channels.",
  icons: {
    icon: "/branding/icon.svg",
  },
};

export default async function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  // docs/settings-appearance-design.md §3.2: read server-side so the very
  // first byte of HTML already carries the right data-theme — no flash of
  // the wrong theme, no blocking inline script needed. resolveThemeId
  // degrades an absent/unrecognized cookie value to the default (§2.3).
  const cookieStore = await cookies();
  const theme = resolveThemeId(cookieStore.get(THEME_COOKIE)?.value);

  return (
    <html lang="en" data-theme={theme}>
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased`}
      >
        {children}
      </body>
    </html>
  );
}
