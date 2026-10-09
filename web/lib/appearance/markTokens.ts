// The CSS custom properties components/Mark.tsx reads, as a pure data
// module — no React import — so a test can check every one exists in
// every theme block without rendering the component. Mirrors the design
// points in docs/design/brand-guidelines.md §5.3 item 5 / §5.4: a
// brand -> brand-2 gradient for the shard, muted for the dotted line and
// top node, fg for the diamond, brand for the hub line, ring and dot.
export const MARK_TOKEN_VARS = ["--color-brand", "--color-brand-2", "--color-muted", "--color-fg"] as const;
