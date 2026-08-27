import type { Metadata } from "next"
import { GeistSans } from "geist/font/sans"
import { GeistMono } from "geist/font/mono"

import { SiteFooter } from "@/components/site-footer"
import { SiteHeader } from "@/components/site-header"

import "./globals.css"

const siteUrl = process.env.NEXT_SITE_URL ?? "https://akash97p.github.io/PrintDrop/"

// Must mirror the basePath logic in next.config.ts: metadata icon paths are
// emitted verbatim, so they have to carry the repository subpath themselves.
const basePath =
  process.env.NEXT_BASE_PATH ??
  (process.env.NODE_ENV === "production" ? "/PrintDrop" : "")

export const metadata: Metadata = {
  metadataBase: new URL(siteUrl),
  title: "PrintDrop — a Wi-Fi flash drive for 3D printers",
  description:
    "PrintDrop plugs into a 3D printer's USB port and appears as an ordinary flash drive, while also serving a web UI over Wi-Fi. Drop a print job from your desk instead of carrying a USB stick.",
  icons: { icon: `${basePath}/logo.webp` },
  openGraph: {
    title: "PrintDrop — a Wi-Fi flash drive for 3D printers",
    description:
      "Drop a print job from your desk instead of carrying a USB stick to the machine.",
    url: "/",
    type: "website",
    images: ["/banner.webp"],
  },
  twitter: {
    card: "summary_large_image",
    title: "PrintDrop",
    description:
      "Drop a print job from your desk instead of carrying a USB stick to the machine.",
    images: ["/banner.webp"],
  },
}

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode
}>) {
  return (
    <html
      lang="en"
      className={`dark ${GeistSans.variable} ${GeistMono.variable}`}
    >
      <body className="min-h-screen font-sans antialiased">
        <SiteHeader />
        <main>{children}</main>
        <SiteFooter />
      </body>
    </html>
  )
}
