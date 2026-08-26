#!/usr/bin/env bash
# Build the GitHub Pages site (website/ — a Next.js static export) into _site/.
#
# Invoked through bash rather than executed directly: this repository is
# developed on a Windows filesystem, which does not carry the exec bit,
# so relying on it silently breaks the build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/_site"

# The prebuild step copies the branding assets from their canonical locations
# (assets/ holds the banner, data/ holds the icon) into website/public/.
cd "$ROOT/website"

if command -v pnpm >/dev/null 2>&1; then
  pnpm install --frozen-lockfile
  pnpm build
else
  npm ci
  npm run build
fi

rm -rf "$OUT"
mkdir -p "$OUT"
cp -a "$ROOT/website/out/." "$OUT/"

# GitHub Pages runs Jekyll over the artifact unless told not to.
touch "$OUT/.nojekyll"

echo "Built site:"
find "$OUT" -maxdepth 1 -type f -printf '  %P (%s bytes)\n' | sort
