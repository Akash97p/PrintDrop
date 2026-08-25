#!/usr/bin/env bash
# Assemble the GitHub Pages site into _site/.
#
# The site is plain static files. This script exists so the branding assets stay
# in one place: the banner lives in assets/ (it is also the README header) and
# the icon lives in data/ (it is also flashed to the device), and both are
# copied in here rather than duplicated in the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/_site"

rm -rf "$OUT"
mkdir -p "$OUT"

cp "$ROOT"/site/*.html "$OUT"/
cp "$ROOT"/site/*.css  "$OUT"/

cp "$ROOT/assets/printdrop_banner.webp" "$OUT/banner.webp"
cp "$ROOT/data/logo.webp"               "$OUT/logo.webp"

# GitHub Pages runs Jekyll over the artifact unless told not to.
touch "$OUT/.nojekyll"

echo "Built site:"
find "$OUT" -type f -printf '  %P (%s bytes)\n' | sort
