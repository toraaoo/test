import path from "path"
import { tanstackRouter } from "@tanstack/router-plugin/vite"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import { defineConfig } from "vite"

// https://vite.dev/config/
export default defineConfig({
  // Required for CEF: assets must resolve relative to the custom scheme origin
  // (hestia://app/) rather than an absolute root path.
  base: "./",
  plugins: [
    // Must come before the React plugin. Generates src/routeTree.gen.ts from
    // the files in src/routes.
    tanstackRouter({ target: "react", autoCodeSplitting: true }),
    react(),
    tailwindcss(),
  ],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
  server: {
    // Matches APP_DEV_SERVER_URL in the backend; launch the app with
    // `./cefapp --dev-url=http://localhost:5173` to point at this dev server.
    port: 5173,
    strictPort: true,
  },
})
