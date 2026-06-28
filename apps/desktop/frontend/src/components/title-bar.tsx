import type { ComponentProps, MouseEvent, ReactNode } from "react"

import { cn } from "@/lib/utils"
import { isCefAvailable } from "@/lib/ipc"
import { windowControls } from "@/lib/api"
import { useAppInfo, useWindowState } from "@/hooks/use-ipc"

/**
 * Custom title bar for the frameless native window. We intentionally don't
 * imitate the full OS chrome — just a slim draggable strip with the brand and a
 * compact set of window controls. Controls are placed per-platform (leading on
 * macOS, trailing on Windows/Linux) and only render inside the CEF shell.
 */
export function TitleBar() {
  const inShell = isCefAvailable()
  const { maximized } = useWindowState()
  const { data } = useAppInfo()
  const isMac = data?.platform === "macos"

  // Double-clicking empty title-bar space maximizes / restores, as on a native
  // window — but not when the click lands on a control button.
  const handleDoubleClick = (event: MouseEvent<HTMLDivElement>) => {
    if (!inShell) return
    if ((event.target as HTMLElement).closest("button")) return
    windowControls.toggleMaximize()
  }

  const brand = (
    <div className="flex items-center gap-2.5">
      <span
        aria-hidden
        className="size-2 rotate-45 bg-signal shadow-[0_0_0_3px_color-mix(in_oklch,var(--signal)_18%,transparent)]"
      />
      <span className="font-mono text-[12px] font-medium tracking-tight">
        cef<span className="text-muted-foreground">/bridge</span>
      </span>
    </div>
  )

  const controls = inShell ? (
    <div className="app-no-drag flex items-center gap-1">
      <WindowButton label="Minimize" onClick={() => windowControls.minimize()}>
        <MinimizeGlyph />
      </WindowButton>
      <WindowButton
        label={maximized ? "Restore" : "Maximize"}
        onClick={() => windowControls.toggleMaximize()}
      >
        {maximized ? <RestoreGlyph /> : <MaximizeGlyph />}
      </WindowButton>
      <WindowButton label="Close" danger onClick={() => windowControls.close()}>
        <CloseGlyph />
      </WindowButton>
    </div>
  ) : null

  return (
    <div
      onDoubleClick={handleDoubleClick}
      className="app-drag flex h-9 shrink-0 items-center gap-3 border-b border-border bg-background/80 px-2.5 select-none"
    >
      {isMac ? (
        <>
          {controls}
          {brand}
        </>
      ) : (
        <>
          {brand}
          <div className="ml-auto">{controls}</div>
        </>
      )}
    </div>
  )
}

function WindowButton({
  label,
  danger,
  className,
  children,
  ...props
}: ComponentProps<"button"> & { label: string; danger?: boolean }) {
  return (
    <button
      type="button"
      aria-label={label}
      title={label}
      className={cn(
        "grid size-7 place-items-center text-muted-foreground transition-colors outline-none",
        "focus-visible:text-foreground",
        danger
          ? "hover:bg-destructive hover:text-white focus-visible:bg-destructive focus-visible:text-white"
          : "hover:bg-accent hover:text-foreground focus-visible:bg-accent",
        className
      )}
      {...props}
    >
      {children}
    </button>
  )
}

// 10×10 glyphs at 1px stroke — crisp and unobtrusive at this button size.
function Glyph({ children }: { children: ReactNode }) {
  return (
    <svg
      width="10"
      height="10"
      viewBox="0 0 10 10"
      fill="none"
      stroke="currentColor"
      strokeWidth="1"
      shapeRendering="crispEdges"
      aria-hidden
    >
      {children}
    </svg>
  )
}

function MinimizeGlyph() {
  return (
    <Glyph>
      <line x1="0" y1="5" x2="10" y2="5" />
    </Glyph>
  )
}

function MaximizeGlyph() {
  return (
    <Glyph>
      <rect x="0.5" y="0.5" width="9" height="9" />
    </Glyph>
  )
}

function RestoreGlyph() {
  return (
    <Glyph>
      <rect x="0.5" y="2.5" width="7" height="7" />
      <path d="M2.5 2.5 V0.5 H9.5 V7.5 H7.5" />
    </Glyph>
  )
}

function CloseGlyph() {
  return (
    <Glyph>
      <line x1="0.5" y1="0.5" x2="9.5" y2="9.5" />
      <line x1="9.5" y1="0.5" x2="0.5" y2="9.5" />
    </Glyph>
  )
}
