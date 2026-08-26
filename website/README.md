# PrintDrop website

The GitHub Pages site: a Next.js app statically exported and served from the
`PrintDrop` repository subpath. The visual language follows the
[AgentNotify](https://akash97p.github.io/agent-notify/) site — dark-only,
monochrome, shadcn/ui (new-york, neutral) components on Tailwind CSS 4, Geist
fonts.

## Develop

```sh
pnpm install
pnpm dev        # http://localhost:3000 — no base path in dev
```

The `predev`/`prebuild` steps copy the branding assets from their canonical
locations (`assets/printdrop_banner.webp`, `data/logo.webp`) into `public/`,
so the repo never carries duplicates. `pnpm dev` needs them, so run the
install at least once before editing.

## Build

```sh
pnpm build      # static export into out/, with the /PrintDrop base path
```

CI (`.github/workflows/pages.yml`) runs `scripts/build-site.sh`, which does
the same and stages the export into `_site/` for GitHub Pages. The base path
is set in `next.config.ts` and mirrored for metadata in `src/app/layout.tsx`;
bump both if the repository is ever renamed.

## Structure

- `src/app/page.tsx` — the whole landing page, one section per block
- `src/components/ui/` — shadcn/ui components (button, badge, card,
  separator, sheet)
- `src/components/site-header.tsx` / `site-footer.tsx` — chrome
- `src/app/globals.css` — theme tokens and the hero `grid-surface` utility
