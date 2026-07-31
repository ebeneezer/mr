# Coprocessor / Deferred UI Contract

## Scope

Applies to:

- deferred macro UI payloads in `coprocessor/MRCoprocessor.*`,
- queueing and playback in `coprocessor/MRCoprocessorDispatch.*`,
- MacroScreenModel, MacroScreenView and DeferredUiRenderGateway,
- VM render-facade and batching bridges used by playback.

Worker ownership and scheduling follow the
[Coprocessor Runtime contract](coprocessor-runtime-contract.md).

## Authority

Deferred macro UI playback owns its authoritative queue, playback order and
screen-model transition state under `DEFERREDUI/playbackQueue` in the central
VM K/V. The VM produces typed commands; the UI-thread playback path rebuilds
short-lived C++ transfer objects and applies them.

## Invariants

- Staged macro UI commands are applied only after their owning staged result is
  accepted.
- Queue ownership, order and UI-thread assumptions remain explicit.
- C++ playback objects are snapshots only; no parallel deque or playback
  registry may retain authoritative values.
- `mrvmUiBeginMacroScreenBatch` and `mrvmUiEndMacroScreenBatch` delimit the
  existing playback batch.
- MacroScreenModel, MacroScreenView and DeferredUiRenderGateway are protected
  producer/consumer boundaries even when their types appear mechanical.
- Playback renders through the approved VM facade and does not call alternate
  direct UI paths.
- Screen mutation epoch checks prevent stale base projection.

## Boundaries

Without explicit maintainer approval:

- No direct UI call replacing a staged command.
- No rendering from a worker thread.
- No incidental change to playback order, lifetime, queue ownership, gateway
  chain, epoch handling or batch boundaries.

## Related contracts

- [Coprocessor Runtime](coprocessor-runtime-contract.md)
- [VM / Intrinsics / Deferred UI](vm-deferred-ui-contract.md)
- [MRMac Execution Session](mrmac-exec-session-contract.md)
- [TVision Integration](tvision-integration-contract.md)

## Required manual tests

- Complete a background staged macro and accept its result.
- Reject a conflicting staged result and verify no playback.
- Verify deferred command ordering and batch boundaries.
- Exercise message-line, macro-screen and overlay projection.
- Verify epoch invalidation and screen redraw behavior.
