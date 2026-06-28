import type { ReactNode } from "react"

import { cn } from "@/lib/utils"

interface PanelProps {
  /** Channel / section identifier, rendered as the mono instrument label. */
  label: string
  /** Optional right-aligned status or metadata for the header strip. */
  aside?: ReactNode
  className?: string
  children: ReactNode
}

/**
 * The shared instrument-panel surface: a hairline-bordered card with a labelled
 * header strip. Every section across the app is built from this so spacing,
 * borders, and the mono channel label stay identical everywhere.
 */
export function Panel({ label, aside, className, children }: PanelProps) {
  return (
    <section
      className={cn(
        "border border-border bg-card text-card-foreground",
        className
      )}
    >
      <header className="flex h-9 items-center justify-between border-b border-border px-3">
        <div className="flex items-center gap-2">
          <span
            aria-hidden
            className="size-1.5 bg-signal shadow-[0_0_0_3px_color-mix(in_oklch,var(--signal)_18%,transparent)]"
          />
          <span className="font-mono text-[11px] tracking-wide text-muted-foreground">
            {label}
          </span>
        </div>
        {aside}
      </header>
      <div className="p-4">{children}</div>
    </section>
  )
}
