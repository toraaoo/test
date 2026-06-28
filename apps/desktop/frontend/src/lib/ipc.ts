// Typed bridge over the CEF message router.
//
// Request/response IPC goes through the standard `window.cefQuery` primitive
// injected by the renderer process. Native -> JS events are delivered as DOM
// CustomEvents dispatched on `window` (see backend/src/core/app/renderer_app.cc),
// where the event name is the channel and `event.detail` is the JSON payload.
//
// The C++ side registers matching handlers with core::ipc::Handle("channel", ...)
// and pushes events with core::ipc::Emit(browser, "channel", value).

declare global {
  interface Window {
    cefQuery?: (options: {
      request: string
      persistent?: boolean
      onSuccess: (response: string) => void
      onFailure: (errorCode: number, errorMessage: string) => void
    }) => number
    cefQueryCancel?: (requestId: number) => void
  }
}

/** Error thrown when an IPC request is rejected by the backend (onFailure). */
export class IpcError extends Error {
  readonly code: number

  constructor(message: string, code: number) {
    super(message)
    this.name = "IpcError"
    this.code = code
  }
}

/** True when running inside the CEF shell (i.e. the bridge is available). */
export function isCefAvailable(): boolean {
  return typeof window !== "undefined" && typeof window.cefQuery === "function"
}

function resolveFallback<TResult>(fallback: TResult | (() => TResult)): TResult {
  return typeof fallback === "function"
    ? (fallback as () => TResult)()
    : fallback
}

export interface InvokeOptions<TResult> {
  /**
   * Value (or factory) to resolve with when the native bridge is unavailable —
   * e.g. running in a plain browser, `vite preview`, or before the desktop app
   * has wired up. Lets the UI degrade gracefully instead of rejecting. When
   * omitted, an unavailable bridge rejects with an IpcError.
   */
  fallback?: TResult | (() => TResult)
}

/**
 * Send a request to the backend and resolve with its (JSON-parsed) response.
 *
 * @param channel  Registered channel name, e.g. "app.info".
 * @param payload  Optional JSON-serializable payload (defaults to null).
 * @param options  Optional behavior, e.g. a {@link InvokeOptions.fallback}.
 */
export function invoke<TResult = unknown>(
  channel: string,
  payload: unknown = null,
  options: InvokeOptions<TResult> = {}
): Promise<TResult> {
  return new Promise<TResult>((resolve, reject) => {
    if (!isCefAvailable()) {
      if ("fallback" in options) {
        resolve(resolveFallback(options.fallback as TResult | (() => TResult)))
        return
      }
      reject(
        new IpcError(
          "window.cefQuery is unavailable (not running inside the CEF shell?). " +
            "Run the desktop app, or launch it with --dev-url to load this dev server.",
          -1
        )
      )
      return
    }

    window.cefQuery!({
      request: JSON.stringify({ channel, payload }),
      onSuccess: (response) => {
        try {
          resolve(JSON.parse(response) as TResult)
        } catch {
          // Backend answered with a bare (non-JSON) string; hand it back raw.
          resolve(response as unknown as TResult)
        }
      },
      onFailure: (code, message) => reject(new IpcError(message, code)),
    })
  })
}

/**
 * Subscribe to a native -> JS event. Returns an unsubscribe function.
 *
 * @param channel  Event name emitted from C++ via core::ipc::Emit.
 * @param handler  Receives the event's JSON payload (`event.detail`).
 */
export function on<TDetail = unknown>(
  channel: string,
  handler: (detail: TDetail) => void
): () => void {
  const listener = (event: Event) => {
    handler((event as CustomEvent<TDetail>).detail)
  }
  window.addEventListener(channel, listener)
  return () => window.removeEventListener(channel, listener)
}
