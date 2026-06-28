import { isCefAvailable } from "@/lib/ipc"

/**
 * Header readout for the native bridge. Live when running inside the CEF shell,
 * detached in a plain browser — mirrors the teal "signal" motif used across the
 * panels. Evaluated once at mount; the bridge presence doesn't change at runtime.
 */
export function ConnectionStatus() {
  const live = isCefAvailable()

  return (
    <div className="flex items-center gap-2 font-mono text-[11px] tracking-wide">
      <span className="relative flex size-1.5">
        {live && (
          <span className="absolute inline-flex size-full animate-ping bg-signal opacity-60" />
        )}
        <span
          className={
            live
              ? "relative inline-flex size-1.5 bg-signal"
              : "relative inline-flex size-1.5 bg-muted-foreground/50"
          }
        />
      </span>
      <span className={live ? "text-foreground" : "text-muted-foreground"}>
        {live ? "bridge · live" : "bridge · detached"}
      </span>
    </div>
  )
}
