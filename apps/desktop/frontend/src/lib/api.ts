// Typed surface for the backend's IPC channels.
//
// Each function wraps a registered C++ channel (see backend/src/features). Add a
// function here whenever you register a new channel with core::ipc::Handle /
// Actions::On so the rest of the app gets an end-to-end typed call.

import { invoke } from "@/lib/ipc"

/** Host platform reported by the backend, used to place window controls. */
export type Platform = "windows" | "macos" | "linux"

/** Identity dictionary returned by the "app.info" channel. */
export interface AppInfo {
  name: string
  id: string
  vendor: string
  version: string
  channel: string
  scheme: string
  platform: Platform
}

/** Placeholder identity used when the native bridge isn't connected. */
const DISCONNECTED_APP_INFO: AppInfo = {
  name: "App (disconnected)",
  id: "—",
  vendor: "—",
  version: "—",
  channel: "—",
  scheme: "—",
  platform: "linux",
}

/**
 * Application identity (channel: "app.info"). Falls back to a placeholder when
 * the native backend isn't connected so the UI can still render.
 */
export function getAppInfo(): Promise<AppInfo> {
  return invoke<AppInfo>("app.info", null, { fallback: DISCONNECTED_APP_INFO })
}

/**
 * Echo round-trip (channel: "app.ping"). Returns the message, or "pong". When
 * the backend isn't connected, echoes locally instead of failing.
 */
export function ping(message?: string): Promise<string> {
  return invoke<string>("app.ping", message ?? null, {
    fallback: () => message || "pong",
  })
}

/** Native top-level window state (channel: "window.state"). */
export interface WindowState {
  maximized: boolean
  minimized: boolean
}

const DETACHED_WINDOW_STATE: WindowState = {
  maximized: false,
  minimized: false,
}

/**
 * Window-management channels driving the frameless native window (the frontend
 * draws its own title bar). Each falls back to a no-op outside the CEF shell so
 * the UI stays inert in a plain browser.
 */
export const windowControls = {
  minimize: () => invoke<null>("window.minimize", null, { fallback: null }),
  toggleMaximize: () => invoke<null>("window.maximize", null, { fallback: null }),
  close: () => invoke<null>("window.close", null, { fallback: null }),
  getState: () =>
    invoke<WindowState>("window.state", null, {
      fallback: DETACHED_WINDOW_STATE,
    }),
}
