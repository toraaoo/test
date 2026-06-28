import type { ReactNode } from "react"
import type { QueryClient } from "@tanstack/react-query"
import { createRootRouteWithContext, Link, Outlet } from "@tanstack/react-router"

import { ConnectionStatus } from "@/components/connection-status"
import { ThemeToggle } from "@/components/theme-toggle"
import { TitleBar } from "@/components/title-bar"

// Context is injected by the router (see src/router.tsx) so route loaders can
// reach the shared QueryClient for prefetching.
export interface RouterContext {
  queryClient: QueryClient
}

export const Route = createRootRouteWithContext<RouterContext>()({
  component: RootLayout,
})

function RootLayout() {
  return (
    <div className="flex h-svh flex-col overflow-hidden">
      <TitleBar />

      <header className="flex h-11 shrink-0 items-stretch border-b border-border bg-background/85 backdrop-blur-sm">
        <nav className="flex items-stretch pl-3">
          <NavLink to="/">overview</NavLink>
          <NavLink to="/ipc">ipc</NavLink>
        </nav>

        <div className="ml-auto flex items-center gap-3 pr-4 pl-6">
          <ConnectionStatus />
          <span aria-hidden className="h-4 w-px bg-border" />
          <ThemeToggle />
        </div>
      </header>

      <main className="flex-1 overflow-y-auto px-6 py-10">
        <div className="mx-auto w-full max-w-2xl">
          <Outlet />
        </div>
      </main>
    </div>
  )
}

function NavLink({ to, children }: { to: string; children: ReactNode }) {
  return (
    <Link
      to={to}
      activeOptions={{ exact: to === "/" }}
      className="relative flex items-center px-3.5 font-mono text-xs tracking-wide text-muted-foreground transition-colors hover:text-foreground"
      activeProps={{
        className:
          "text-foreground after:absolute after:inset-x-0 after:bottom-[-1px] after:h-px after:bg-signal",
      }}
    >
      {children}
    </Link>
  )
}
