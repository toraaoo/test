import * as React from "react"

import { cn } from "@/lib/utils"

/**
 * Text input tuned to match the Button: squared corners, compact height, and a
 * teal focus ring drawn from the shared --ring token.
 */
function Input({ className, type, ...props }: React.ComponentProps<"input">) {
  return (
    <input
      type={type}
      data-slot="input"
      className={cn(
        "flex h-8 w-full min-w-0 border border-input bg-background px-2.5 text-xs",
        "font-mono text-foreground placeholder:text-muted-foreground/70",
        "transition-[color,box-shadow] outline-none",
        "focus-visible:border-ring focus-visible:ring-1 focus-visible:ring-ring/50",
        "disabled:pointer-events-none disabled:opacity-50",
        "aria-invalid:border-destructive aria-invalid:ring-1 aria-invalid:ring-destructive/20",
        className
      )}
      {...props}
    />
  )
}

export { Input }
