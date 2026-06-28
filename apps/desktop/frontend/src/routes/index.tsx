import { createFileRoute } from "@tanstack/react-router"

import { getAppInfo, type AppInfo } from "@/lib/api"
import { ipcKeys, useAppInfo } from "@/hooks/use-ipc"
import { PageHeader } from "@/components/page-header"
import { Panel } from "@/components/ui/panel"

export const Route = createFileRoute("/")({
  // Prefetch the identity through the shared QueryClient so the component reads
  // it from cache. getAppInfo falls back to a placeholder when disconnected, so
  // this is safe to run everywhere.
  loader: ({ context: { queryClient } }) =>
    queryClient.ensureQueryData({
      queryKey: ipcKeys.appInfo,
      queryFn: getAppInfo,
    }),
  component: HomePage,
})

function HomePage() {
  const { data, isPending, error } = useAppInfo()

  return (
    <div className="flex flex-col gap-8">
      <PageHeader
        eyebrow="overview"
        title="Frontend shell is ready"
        description="React, TanStack Router, and TanStack Query talking to the C++ backend over the IPC bridge. This panel reads identity straight off the native channel."
      />

      <Panel label="app.info">
        {error ? (
          <p className="text-sm text-destructive">{error.message}</p>
        ) : isPending ? (
          <p className="font-mono text-xs text-muted-foreground">loading…</p>
        ) : (
          <dl className="flex flex-col">
            {(Object.entries(data) as [keyof AppInfo, string][]).map(
              ([key, value]) => (
                <div
                  key={key}
                  className="grid grid-cols-[7rem_1fr] items-baseline gap-x-6 border-border py-2 not-first:border-t"
                >
                  <dt className="font-mono text-[11px] tracking-wide text-muted-foreground">
                    {key}
                  </dt>
                  <dd className="truncate font-mono text-xs text-foreground">
                    {value}
                  </dd>
                </div>
              )
            )}
          </dl>
        )}
      </Panel>
    </div>
  )
}
