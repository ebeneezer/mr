# Macro Debugger Exec Session Notes

## Purpose

These notes capture Exec Session follow-up topics that should be owned by the
Macro Debugger card, not by the base Exec Session runtime.

The base Exec Session layer should remain a small execution, ownership,
cancellation and result substrate. Ergonomic inspection, audit views and
source-level control belong to the Macro Debugger.

## Session Observability

The Macro Debugger should provide the native observability surface for Exec
Sessions.

Required information:

- session id,
- owner kind and owner id,
- macro spec,
- source package where available,
- start time,
- end time,
- current state,
- result status,
- cancellation reason,
- skip reason for scheduled runs,
- parent session or caller frame where available,
- last VM error text where available.

The debugger UI should be able to show cause chains such as:

- started by menu,
- started by scheduled tick,
- started by modeless UI callback,
- skipped because previous session is still active,
- canceled by owner shutdown,
- closed because a modeless window was closed.

This is also useful for audit mode. Audit mode does not need to debug MRMAC
source lines. It needs a reliable session timeline and enough ownership data
to explain why something ran, skipped, stopped or failed.

The base runtime may expose raw status data, but it should not grow a second
debugger UI or an ergonomics layer. The Macro Debugger owns presentation,
filtering, timelines, session drill-down and audit workflows.

## Debugger Integration

The Macro Debugger must run on top of Exec Sessions. It must not introduce a
parallel macro runner.

Debugger controls map naturally to session control:

- start creates an Exec Session,
- pause stops at cooperative VM boundaries,
- step resumes one debuggable unit,
- continue resumes until the next stop condition,
- cancel requests session cancellation,
- inspect reads the session-owned VM/debug snapshot.

Breakpoints, source maps, watches, locals, call frames and step mode are
debugger concepts. They should not be baked into the base Exec Session model
except as minimal hooks needed to pause, resume and report snapshots.

Nested `RUN_MACRO(...)` calls should appear as debugger frames. If a called
macro has no source map, the debugger should show an external or opaque frame
instead of inventing source positions.

Side effects remain real in the first debugger slice. A preview or staged
debug mode would be a separate architecture decision and must not be smuggled
into the initial debugger integration.

Non-goals for the base Exec Session layer:

- source-level debugger UI,
- breakpoint storage,
- watch expression management,
- source-map ownership,
- audit timeline presentation,
- alternate macro execution semantics.
