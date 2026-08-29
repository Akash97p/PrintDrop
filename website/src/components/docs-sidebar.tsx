import Link from "next/link"

import { docsBySection } from "@/lib/docs"
import { cn } from "@/lib/utils"

export function DocsSidebar({ activeSlug }: { activeSlug?: string }) {
  return (
    <nav className="space-y-7" aria-label="Documentation navigation">
      {Object.entries(docsBySection()).map(([section, entries]) => (
        <div key={section}>
          <p className="mb-2 px-2 text-[11px] font-medium uppercase tracking-[0.14em] text-muted-foreground">
            {section}
          </p>
          <div className="grid gap-0.5">
            {entries.map((doc) => (
              <Link
                key={doc.slug}
                href={`/docs/${doc.slug}/`}
                className={cn(
                  "rounded-md px-2 py-1.5 text-sm text-muted-foreground transition-colors hover:bg-accent hover:text-foreground",
                  activeSlug === doc.slug && "bg-accent text-foreground",
                )}
              >
                {doc.title}
              </Link>
            ))}
          </div>
        </div>
      ))}
    </nav>
  )
}
