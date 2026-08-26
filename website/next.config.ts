import type { NextConfig } from "next"

// GitHub Pages serves the site from a project subpath (…github.io/PrintDrop/),
// so every URL must carry the repository name when building for production.
// Local dev runs at the root, where a basePath would only get in the way.
const basePath =
  process.env.NEXT_BASE_PATH ??
  (process.env.NODE_ENV === "production" ? "/PrintDrop" : "")

const nextConfig: NextConfig = {
  // The whole point is a statically exportable site for GitHub Pages.
  output: "export",
  trailingSlash: true,
  ...(basePath ? { basePath } : {}),
  images: {
    // No image optimizer exists on a static export; ship the (already small)
    // webp assets as they are.
    unoptimized: true,
  },
}

export default nextConfig
