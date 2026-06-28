# Frontend

React + TypeScript + Vite app for the CEF desktop shell. It talks to the C++
backend over the IPC bridge and is decoupled from it — they share nothing but
the channel contract.

Stack: **React 19**, **TanStack Router** (file-based), **TanStack Query**,
**Tailwind v4**, **shadcn/ui**.

## Develop

```bash
bun install        # or npm install
bun run dev        # vite dev server on http://localhost:5173
```

Then launch the desktop app pointed at the dev server for live IPC:

```bash
./cefapp --dev-url=http://localhost:5173
```

`bun run build` produces `dist/`, which the backend stages into its web root and
serves from the custom `cefapp://app/` scheme. Other scripts: `bun run lint`,
`bun run typecheck`, `bun run format`.

## Talking to the backend

The bridge lives in `src/lib/`:

- **`ipc.ts`** — low-level wrapper over `window.cefQuery`. `invoke(channel,
  payload, { fallback })` does request/response; `on(channel, cb)` subscribes to
  native → JS events (delivered as DOM CustomEvents). `isCefAvailable()` reports
  whether the native bridge is present.
- **`api.ts`** — typed functions per channel (`getAppInfo()`, `ping()`). Add one
  here whenever you register a new C++ channel.
- **`query-client.ts`** — the shared TanStack Query client.

`src/hooks/use-ipc.ts` exposes React Query hooks (`useAppInfo`, `usePing`) and
`useIpcEvent(channel, handler)` for event subscriptions.

```ts
import { invoke, on } from "@/lib/ipc"

const info = await invoke("app.info")              // request/response
const off = on("download.progress", (d) => …)      // native -> JS event
```

### Disconnected fallback

When the native bridge isn't available (a plain browser, `vite preview`, or
before the desktop app wires up), `invoke` rejects unless you pass a `fallback`.
The typed API supplies sensible fallbacks (`getAppInfo` returns a placeholder,
`ping` echoes locally) so the UI renders without throwing.

## Routing

File-based routes live in `src/routes/`; the `@tanstack/router-plugin` Vite
plugin regenerates `src/routeTree.gen.ts` on dev and build. It's committed
(rather than gitignored) because `bun run build` runs `tsc` before `vite`, so the
file must exist for the typecheck step. `src/router.tsx` wires the route tree to
the shared QueryClient via router context, so loaders can prefetch through the
cache.

- `__root.tsx` — app layout + nav
- `index.tsx` — home; loads `app.info`
- `ipc.tsx` — IPC playground (ping + live event log)

## Adding shadcn components

```bash
npx shadcn@latest add button
```

Components land in `src/components/ui` and import as `@/components/ui/button`.
