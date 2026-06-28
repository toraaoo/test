import { useState } from "react"
import { createFileRoute } from "@tanstack/react-router"
import { WarningCircle } from "@phosphor-icons/react"

import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Panel } from "@/components/ui/panel"
import { PageHeader } from "@/components/page-header"
import { isCefAvailable } from "@/lib/ipc"
import { useIpcEvent, usePing } from "@/hooks/use-ipc"

export const Route = createFileRoute("/ipc")({
  component: IpcPlayground,
})

interface EventLogEntry {
  channel: string
  detail: unknown
  at: string
}

function IpcPlayground() {
  const [message, setMessage] = useState("hello")
  const ping = usePing()

  const [events, setEvents] = useState<EventLogEntry[]>([])

  // Example native -> JS subscription. Emit from C++ with
  // core::ipc::Emit(browser, "demo.event", value) to see entries appear.
  useIpcEvent("demo.event", (detail) => {
    setEvents((prev) =>
      [
        { channel: "demo.event", detail, at: new Date().toLocaleTimeString() },
        ...prev,
      ].slice(0, 50)
    )
  })

  return (
    <div className="flex flex-col gap-8">
      <PageHeader
        eyebrow="ipc"
        title="IPC playground"
        description="Request/response over window.cefQuery, and native → JS events delivered as DOM CustomEvents. Exercise both halves of the bridge here."
      />

      {!isCefAvailable() && (
        <div className="flex items-start gap-2.5 border border-destructive/40 bg-destructive/10 px-3 py-2.5 text-xs text-destructive">
          <WarningCircle weight="fill" className="mt-px size-4 shrink-0" />
          <p className="leading-relaxed">
            Running outside the CEF shell — calls will fall back to local echoes.
            Launch the desktop app, or run it with{" "}
            <code className="font-mono">--dev-url</code>.
          </p>
        </div>
      )}

      <Panel label="app.ping">
        <div className="flex items-center gap-2">
          <Input
            value={message}
            onChange={(event) => setMessage(event.target.value)}
            placeholder="message"
            onKeyDown={(event) => {
              if (event.key === "Enter" && !ping.isPending) ping.mutate(message)
            }}
          />
          <Button
            size="lg"
            onClick={() => ping.mutate(message)}
            disabled={ping.isPending}
          >
            {ping.isPending ? "sending…" : "send"}
          </Button>
        </div>
        {ping.data !== undefined && (
          <pre className="mt-3 overflow-auto border border-border bg-muted/50 px-3 py-2 font-mono text-[11px] leading-relaxed">
            {JSON.stringify(ping.data, null, 2)}
          </pre>
        )}
        {ping.error && (
          <p className="mt-3 font-mono text-xs text-destructive">
            {ping.error.message}
          </p>
        )}
      </Panel>

      <Panel
        label="event.log"
        aside={
          <span className="font-mono text-[11px] text-muted-foreground tabular-nums">
            {events.length} / 50
          </span>
        }
      >
        {events.length === 0 ? (
          <p className="font-mono text-xs text-muted-foreground">
            awaiting <span className="text-foreground">demo.event</span> …
          </p>
        ) : (
          <ul className="flex flex-col">
            {events.map((entry, index) => (
              <li
                key={index}
                className="flex items-baseline gap-3 border-border py-1.5 font-mono text-[11px] not-first:border-t"
              >
                <span className="shrink-0 text-muted-foreground tabular-nums">
                  {entry.at}
                </span>
                <span className="shrink-0 text-signal">{entry.channel}</span>
                <span className="truncate text-foreground">
                  {JSON.stringify(entry.detail)}
                </span>
              </li>
            ))}
          </ul>
        )}
      </Panel>
    </div>
  )
}
