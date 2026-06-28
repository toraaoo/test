import { QueryClient } from "@tanstack/react-query"

// Single shared client. IPC calls are local (no network), so failures are
// almost always a missing handler or the bridge being unavailable outside the
// shell — retrying those is just noise, hence retry: false.
export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: false,
      refetchOnWindowFocus: false,
      staleTime: 30_000,
    },
  },
})
