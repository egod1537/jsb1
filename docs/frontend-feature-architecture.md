# Frontend feature boundaries

The frontend dependency audit found route pages owning server calls and business state, cross-feature imports targeting internal files, a PX4-ID category parser, and a duplicated Roll telemetry catalog. The refactored direction is:

`App/routes -> pages -> feature public API -> feature internals -> api/shared components/types`

- `pages/` contains only route-level exports. Screen state and use-case composition live in `features/*Screen.tsx`.
- Every feature exposes its supported cross-feature surface through `features/<name>/index.ts`. Cross-feature deep imports are guarded by `architecture.test.ts`.
- Components use `api/client.ts`; browser URL construction and `fetch` remain transport concerns there.
- Parameter controls map backend contract DTOs to `RuntimeParameterViewModel`. Categories come only from contract `category`, `group`, or `module` metadata; parameter IDs are opaque.
- Plot signal labels, units, groups, symbols, and descriptions come from `SignalMetadata`. The frontend keeps plot presets as presentation configuration but no longer contains a telemetry semantic catalog.
- Scenario parsing and inspection remain in the scenarios feature. Run creation consumes scenario DTO fields and delegates parameter editing/validation to the parameters feature.
- Plot state, presets, data sources, variants, timeline, and inter-run overlays remain reusable inside the plots feature.

Server state continues to use the existing hook and request pattern; no additional state framework was introduced. Feature-local CSS now covers parameters, scenarios, plots, and run screens, with shared layout tokens remaining in `styles.css`.
