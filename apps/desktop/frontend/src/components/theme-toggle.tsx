import { Desktop, Moon, Sun } from "@phosphor-icons/react"

import { Button } from "@/components/ui/button"
import { useTheme } from "@/components/theme-provider"

const ORDER = ["light", "dark", "system"] as const
const ICONS = {
  light: Sun,
  dark: Moon,
  system: Desktop,
} as const

/**
 * Cycles light → dark → system. The theme provider also honours the bare "d"
 * shortcut; this surfaces the control so it's discoverable.
 */
export function ThemeToggle() {
  const { theme, setTheme } = useTheme()
  const Icon = ICONS[theme]

  const cycle = () => {
    const next = ORDER[(ORDER.indexOf(theme) + 1) % ORDER.length]
    setTheme(next)
  }

  return (
    <Button
      variant="ghost"
      size="icon-sm"
      onClick={cycle}
      aria-label={`Theme: ${theme}. Switch.`}
      title={`Theme: ${theme}`}
    >
      <Icon weight="bold" />
    </Button>
  )
}
