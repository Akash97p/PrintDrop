import { ExternalLink } from "lucide-react"

import { DocsSidebar } from "@/components/docs-sidebar"
import { Separator } from "@/components/ui/separator"
import type { RenderedDoc } from "@/lib/docs"

export function DocsShell({ doc }: { doc: RenderedDoc }) {
  return (
    <div className="mx-auto grid max-w-[1500px] gap-10 px-4 py-8 sm:px-6 lg:grid-cols-[240px_minmax(0,760px)] xl:grid-cols-[240px_minmax(0,760px)_220px]">
      <aside className="sticky top-20 hidden max-h-[calc(100vh-6rem)] overflow-y-auto pr-4 lg:block">
        <DocsSidebar activeSlug={doc.slug} />
      </aside>
      <div className="min-w-0">
        <details className="mb-8 rounded-lg border bg-card p-4 lg:hidden">
          <summary className="cursor-pointer text-sm font-medium">
            Browse documentation
          </summary>
          <div className="mt-5 border-t pt-5">
            <DocsSidebar activeSlug={doc.slug} />
          </div>
        </details>
        <article
          className="docs-prose"
          dangerouslySetInnerHTML={{ __html: doc.html }}
        />
        <Separator className="mt-12" />
        <a
          className="mt-5 inline-flex items-center gap-2 text-sm text-muted-foreground hover:text-foreground"
          href={doc.sourceUrl}
          target="_blank"
          rel="noopener noreferrer"
        >
          Edit this page on GitHub <ExternalLink className="size-3.5" />
        </a>
      </div>
      <aside className="sticky top-20 hidden max-h-[calc(100vh-6rem)] overflow-y-auto xl:block">
        {doc.toc.length > 0 && (
          <nav aria-label="On this page">
            <p className="mb-3 text-xs font-medium text-foreground">On this page</p>
            <div className="grid gap-2 border-l pl-4">
              {doc.toc.map((item) => (
                <a
                  key={item.id}
                  href={`#${item.id}`}
                  className={
                    item.level === 3
                      ? "pl-3 text-xs leading-5 text-muted-foreground hover:text-foreground"
                      : "text-xs leading-5 text-muted-foreground hover:text-foreground"
                  }
                >
                  {item.text}
                </a>
              ))}
            </div>
          </nav>
        )}
      </aside>
    </div>
  )
}
