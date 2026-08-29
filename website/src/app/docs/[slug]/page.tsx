import type { Metadata } from "next"
import { notFound } from "next/navigation"

import { DocsShell } from "@/components/docs-shell"
import { docs, getDoc } from "@/lib/docs"

export const dynamicParams = false

export function generateStaticParams() {
  return docs.map((doc) => ({ slug: doc.slug }))
}

export async function generateMetadata({
  params,
}: {
  params: Promise<{ slug: string }>
}): Promise<Metadata> {
  const { slug } = await params
  const doc = docs.find((item) => item.slug === slug)
  return doc
    ? { title: `${doc.title} · PrintDrop`, description: doc.description }
    : {}
}

export default async function DocumentationPage({
  params,
}: {
  params: Promise<{ slug: string }>
}) {
  const { slug } = await params
  const doc = await getDoc(slug)
  if (!doc) notFound()
  return <DocsShell doc={doc} />
}
