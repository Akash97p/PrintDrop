import type { Metadata } from "next"
import { GeistSans } from "geist/font/sans"
import { GeistMono } from "geist/font/mono"

import { SiteFooter } from "@/components/site-footer"
import { SiteHeader } from "@/components/site-header"

import "./globals.css"

const siteUrl =
  process.env.NEXT_SITE_URL ?? "https://kabani-tech.github.io/PrintDrop/"

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
  icons: {
    icon: [
      { url: `${basePath}/favicon.ico` },
      { url: `${basePath}/favicon-32x32.png`, sizes: "32x32", type: "image/png" },
      { url: `${basePath}/logo.webp`, sizes: "512x512", type: "image/webp" },
    ],
    apple: [{ url: `${basePath}/apple-touch-icon.png`, sizes: "180x180", type: "image/png" }],
  },
  manifest: `${basePath}/site.webmanifest`,
  openGraph: {
    title: "PrintDrop — a Wi-Fi flash drive for 3D printers",
    description:
      "Drop a print job from your desk instead of carrying a USB stick to the machine.",
    url: "/",
    type: "website",
    images: [
      {
        url: "/og.webp",
        width: 1200,
        height: 630,
        alt: "PrintDrop — a Wi-Fi flash drive for 3D printers",
      },
    ],
  },
  twitter: {
    card: "summary_large_image",
    title: "PrintDrop",
    description:
      "Drop a print job from your desk instead of carrying a USB stick to the machine.",
    images: ["/og.webp"],
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
      suppressHydrationWarning
      className={`${GeistSans.variable} ${GeistMono.variable}`}
    >
      <head>
        {/* Sync .dark class with system preference before first paint and on change */}
        <script
          dangerouslySetInnerHTML={{
            __html: `(function(){try{var m=window.matchMedia('(prefers-color-scheme: dark)');function a(v){document.documentElement.classList.toggle('dark',v);}a(m.matches);var h=function(e){a(e.matches);};if(m.addEventListener)m.addEventListener('change',h);else if(m.addListener)m.addListener(h);}catch(e){}})();`,
          }}
        />
      </head>
      <body className="min-h-screen font-sans antialiased">
        <SiteHeader />
        <main>{children}</main>
        <SiteFooter />
      </body>
    </html>
  )
}
