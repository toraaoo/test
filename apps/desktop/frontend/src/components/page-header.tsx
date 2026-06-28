interface PageHeaderProps {
  /** Mono eyebrow shown above the title (e.g. a route or domain tag). */
  eyebrow: string
  title: string
  description: string
}

/** Consistent page intro: mono eyebrow, title, and a muted one-liner. */
export function PageHeader({ eyebrow, title, description }: PageHeaderProps) {
  return (
    <div className="flex flex-col gap-1.5">
      <span className="font-mono text-[11px] tracking-[0.18em] text-signal uppercase">
        {eyebrow}
      </span>
      <h1 className="text-xl font-medium tracking-tight">{title}</h1>
      <p className="max-w-prose text-sm leading-relaxed text-muted-foreground">
        {description}
      </p>
    </div>
  )
}
