// React bindings for the IPC bridge: TanStack Query hooks for request/response
// channels, plus a small effect hook for native -> JS events.

import { useEffect, useRef, useState } from "react"
import { useMutation, useQuery } from "@tanstack/react-query"

import { getAppInfo, ping, windowControls, type WindowState } from "@/lib/api"
import { on } from "@/lib/ipc"

/** Centralized query keys so caches can be invalidated consistently. */
export const ipcKeys = {
  appInfo: ["app", "info"] as const,
}

/** Query the application identity (channel: "app.info"). */
export function useAppInfo() {
  return useQuery({
    queryKey: ipcKeys.appInfo,
    queryFn: getAppInfo,
  })
}

/** Mutation wrapper around the "app.ping" echo channel. */
export function usePing() {
  return useMutation({
    mutationFn: (message?: string) => ping(message),
  })
}

/**
 * Track the native window's maximize state. Seeds from the "window.state"
 * channel on mount, then stays in sync via the pushed "window.state" event
 * (emitted on every bounds change: maximize, restore, snap).
 */
export function useWindowState() {
  const [state, setState] = useState<WindowState>({
    maximized: false,
    minimized: false,
  })

  useEffect(() => {
    let active = true
    windowControls.getState().then((next) => {
      if (active) setState(next)
    })
    return () => {
      active = false
    }
  }, [])

  useIpcEvent<WindowState>("window.state", setState)

  return state
}

/**
 * Subscribe to a native -> JS event for the lifetime of the component. The
 * handler is kept in a ref so re-renders don't re-subscribe.
 */
export function useIpcEvent<TDetail = unknown>(
  channel: string,
  handler: (detail: TDetail) => void
) {
  const handlerRef = useRef(handler)
  useEffect(() => {
    handlerRef.current = handler
  })

  useEffect(() => {
    return on<TDetail>(channel, (detail) => handlerRef.current(detail))
  }, [channel])
}
