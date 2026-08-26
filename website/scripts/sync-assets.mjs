// Copies the branding assets from their canonical locations into public/ so
// that local dev, local builds and CI all serve identical files. The banner
// lives in assets/ (it is also the README header) and the icon lives in data/
// (it is also flashed to the device) — neither is duplicated in the repo.
import { copyFileSync, mkdirSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"

const here = dirname(fileURLToPath(import.meta.url))
const website = join(here, "..")
const repo = join(website, "..")

mkdirSync(join(website, "public"), { recursive: true })

copyFileSync(
  join(repo, "assets", "printdrop_banner.webp"),
  join(website, "public", "banner.webp")
)
copyFileSync(join(repo, "data", "logo.webp"), join(website, "public", "logo.webp"))

console.log("Synced branding assets into website/public/")
