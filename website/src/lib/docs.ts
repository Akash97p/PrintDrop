import fs from "node:fs"
import path from "node:path"
import type { Element, Root } from "hast"
import rehypeSlug from "rehype-slug"
import rehypeStringify from "rehype-stringify"
import remarkGfm from "remark-gfm"
import remarkParse from "remark-parse"
import remarkRehype from "remark-rehype"
import { unified } from "unified"
import { visit } from "unist-util-visit"

export type DocDefinition = {
  source: string
  slug: string
  title: string
  section: string
  description: string
}

export const docs: DocDefinition[] = [
  {
    source: "README.md",
    slug: "getting-started",
    title: "Getting started",
    section: "Start here",
    description: "Hardware, wiring, flashing, first run, and everyday use.",
  },
  {
    source: "docs/flashing.md",
    slug: "flashing",
    title: "Flashing",
    section: "Start here",
    description: "Enter download mode and recover a board reliably.",
  },
  {
    source: "docs/hardware.md",
    slug: "hardware",
    title: "Hardware",
    section: "Build the device",
    description: "Board requirements, SD wiring, power, LED, and button.",
  },
  {
    source: "docs/sdio.md",
    slug: "sdio",
    title: "SDIO 4-bit",
    section: "Build the device",
    description: "Six-wire migration and the faster SD card path.",
  },
  {
    source: "docs/discovery.md",
    slug: "discovery",
    title: "Network discovery",
    section: "Use PrintDrop",
    description: "Find the device with mDNS, LLMNR, or a fixed address.",
  },
  {
    source: "docs/auth.md",
    slug: "authentication",
    title: "Authentication",
    section: "Use PrintDrop",
    description: "Protect the web UI and API with credentials stored in NVS.",
  },
  {
    source: "docs/ota.md",
    slug: "ota",
    title: "OTA updates",
    section: "Use PrintDrop",
    description: "Update firmware over HTTP or from the SD card.",
  },
  {
    source: "docs/architecture.md",
    slug: "architecture",
    title: "Architecture",
    section: "Engineering",
    description: "USB and Wi-Fi arbitration, modules, partitions, and performance.",
  },
  {
    source: "docs/bugs.md",
    slug: "bugs",
    title: "Bugs and root causes",
    section: "Engineering",
    description: "The failures found during the port and why each fix exists.",
  },
  {
    source: "CONTRIBUTING.md",
    slug: "contributing",
    title: "Contributing",
    section: "Engineering",
    description: "Build setup, branch model, constraints, and test expectations.",
  },
]

export type RenderedDoc = DocDefinition & {
  html: string
  toc: { level: number; id: string; text: string }[]
  sourceUrl: string
}

const repositoryUrl = "https://github.com/Akash97p/PrintDrop"
const repoRoot = path.resolve(process.cwd(), "..")
const sourceToSlug = new Map(docs.map((doc) => [doc.source, doc.slug]))

function siteBasePath() {
  return (
    process.env.NEXT_BASE_PATH ??
    (process.env.NODE_ENV === "production" ? "/PrintDrop" : "")
  )
}

function resolveRepoPath(sourcePath: string, rawPath: string) {
  const relative = path.posix.normalize(
    path.posix.join(path.posix.dirname(sourcePath), rawPath),
  )

  if (sourceToSlug.has(relative) || fs.existsSync(path.join(repoRoot, relative))) {
    return relative
  }

  const fromRoot = path.posix.normalize(rawPath.replace(/^\.\//, ""))
  return sourceToSlug.has(fromRoot) || fs.existsSync(path.join(repoRoot, fromRoot))
    ? fromRoot
    : relative
}

function rewriteLinks(sourcePath: string) {
  return () => (tree: Root) => {
    visit(tree, "element", (node: Element) => {
      if (node.tagName === "a" && typeof node.properties?.href === "string") {
        const href = node.properties.href
        if (/^(https?:|mailto:|#)/.test(href)) {
          if (/^https?:/.test(href)) {
            node.properties.target = "_blank"
            node.properties.rel = ["noopener", "noreferrer"]
          }
          return
        }

        const [rawPath, fragment] = href.split("#", 2)
        const normalized = resolveRepoPath(sourcePath, rawPath)
        const slug = sourceToSlug.get(normalized)
        node.properties.href = slug
          ? `${siteBasePath()}/docs/${slug}/${fragment ? `#${fragment}` : ""}`
          : `${repositoryUrl}/blob/main/${normalized}${fragment ? `#${fragment}` : ""}`
        if (!slug) {
          node.properties.target = "_blank"
          node.properties.rel = ["noopener", "noreferrer"]
        }
        return
      }

      if (node.tagName === "img" && typeof node.properties?.src === "string") {
        const src = node.properties.src
        if (/^(https?:|data:)/.test(src)) return
        const normalized = resolveRepoPath(sourcePath, src)
        node.properties.src = `${repositoryUrl}/raw/main/${normalized}`
      }
    })
  }
}

function plainText(value: string) {
  return value
    .replace(/<code>(.*?)<\/code>/g, "$1")
    .replace(/<[^>]+>/g, "")
    .replaceAll("&amp;", "&")
    .replaceAll("&lt;", "<")
    .replaceAll("&gt;", ">")
    .replaceAll("&#x27;", "'")
    .replaceAll("&quot;", '"')
}

export async function getDoc(slug: string): Promise<RenderedDoc | null> {
  const definition = docs.find((doc) => doc.slug === slug)
  if (!definition) return null

  const sourceMarkdown = fs.readFileSync(
    path.join(repoRoot, definition.source),
    "utf8",
  )
  const markdown = /^#\s+.+$/m.test(sourceMarkdown)
    ? sourceMarkdown
    : `# ${definition.title}\n\n${sourceMarkdown}`
  const rendered = await unified()
    .use(remarkParse)
    .use(remarkGfm)
    .use(remarkRehype)
    .use(rehypeSlug)
    .use(rewriteLinks(definition.source))
    .use(rehypeStringify)
    .process(markdown)
  const html = String(rendered)
  const toc = Array.from(
    html.matchAll(/<h([23]) id="([^"]+)">([\s\S]*?)<\/h\1>/g),
  ).map((match) => ({
    level: Number(match[1]),
    id: match[2],
    text: plainText(match[3]),
  }))

  return {
    ...definition,
    html,
    toc,
    sourceUrl: `${repositoryUrl}/blob/main/${definition.source}`,
  }
}

export function docsBySection() {
  return docs.reduce<Record<string, DocDefinition[]>>((groups, doc) => {
    const section = groups[doc.section] ?? []
    section.push(doc)
    groups[doc.section] = section
    return groups
  }, {})
}
