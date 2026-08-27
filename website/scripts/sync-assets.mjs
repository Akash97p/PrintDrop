// Copies the branding assets from their canonical locations into public/ so
// that local dev, local builds and CI all serve identical files.
// README keeps the light banner (black text on white) as assets/printdrop_banner.webp;
// the dark website uses the white-on-dark variant.
import { copyFileSync, existsSync, mkdirSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"

const here = dirname(fileURLToPath(import.meta.url))
const website = join(here, "..")
const repo = join(website, "..")

mkdirSync(join(website, "public"), { recursive: true })

// Website is dark-only (#030303) — prefer the white-on-dark banner if present.
const darkBanner = join(repo, "assets", "printdrop_banner_dark.webp")
const lightBanner = join(repo, "assets", "printdrop_banner.webp")
copyFileSync(
  existsSync(darkBanner) ? darkBanner : lightBanner,
  join(website, "public", "banner.webp")
)
// Website header is dark — prefer the white icon.
const whiteLogo = join(repo, "assets", "printdrop_logo_white.webp")
const blackLogo = join(repo, "assets", "printdrop_logo.webp")
const dataLogo = join(repo, "data", "logo.webp")
const logoSrc = existsSync(whiteLogo) ? whiteLogo : existsSync(blackLogo) ? blackLogo : dataLogo
copyFileSync(logoSrc, join(website, "public", "logo.webp"))

console.log("Synced branding assets into website/public/")
