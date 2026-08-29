import type { Metadata } from "next"
import Link from "next/link"
import { ArrowRight, BookOpen, Cpu, Radio, Wrench } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import {
  Card,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { docsBySection } from "@/lib/docs"

export const metadata: Metadata = {
  title: "Documentation · PrintDrop",
  description:
    "Build, flash, configure, use, and understand the PrintDrop Wi-Fi flash drive.",
}

const sectionIcons = {
  "Start here": BookOpen,
  "Build the device": Wrench,
  "Use PrintDrop": Radio,
  Engineering: Cpu,
} as const

export default function DocumentationIndex() {
  return (
    <div className="mx-auto max-w-6xl px-4 py-16 sm:px-6 sm:py-24">
      <Badge variant="secondary">Documentation</Badge>
      <h1 className="mt-5 max-w-3xl text-5xl font-semibold tracking-[-0.05em]">
        From bare board to wireless print jobs.
      </h1>
      <p className="mt-6 max-w-2xl text-lg leading-8 text-muted-foreground">
        Wire the SD card, flash the firmware, bring PrintDrop onto your network,
        and understand the storage rules that keep the printer&apos;s filesystem safe.
      </p>
      <div className="mt-14 grid gap-5 md:grid-cols-2">
        {Object.entries(docsBySection()).map(([section, entries]) => {
          const Icon =
            sectionIcons[section as keyof typeof sectionIcons] ?? BookOpen
          return (
            <Card key={section}>
              <CardHeader>
                <div className="mb-3 flex size-9 items-center justify-center rounded-md border bg-background">
                  <Icon className="size-4" />
                </div>
                <CardTitle>{section}</CardTitle>
                <CardDescription>
                  {entries.length} {entries.length === 1 ? "guide" : "guides and references"}
                </CardDescription>
                <div className="mt-5 grid gap-1">
                  {entries.map((doc) => (
                    <Link
                      key={doc.slug}
                      href={`/docs/${doc.slug}/`}
                      className="group flex items-center justify-between rounded-md px-3 py-2 text-sm text-muted-foreground hover:bg-accent hover:text-foreground"
                    >
                      <span>
                        <span className="block font-medium text-foreground">
                          {doc.title}
                        </span>
                        <span className="mt-0.5 block text-xs">
                          {doc.description}
                        </span>
                      </span>
                      <ArrowRight className="size-4 shrink-0 opacity-0 transition-opacity group-hover:opacity-100" />
                    </Link>
                  ))}
                </div>
              </CardHeader>
            </Card>
          )
        })}
      </div>
    </div>
  )
}
