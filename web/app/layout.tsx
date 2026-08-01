import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import "./globals.css";

import { DEFAULT_THEME_ID } from "@/lib/appearance/themes";

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

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    // Hardcoded for now (docs/settings-appearance-design.md §5 step 1 is a
    // visual no-op by design) — becomes a cookie read in step 2.
    <html lang="en" data-theme={DEFAULT_THEME_ID}>
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased`}
      >
        {children}
      </body>
    </html>
  );
}
