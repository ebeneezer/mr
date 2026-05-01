# Coprocessor / Deferred UI / Macro Screen Contract

## Scope

Applies to:

- `coprocessor/MRCoprocessor.hpp`
- `coprocessor/MRCoprocessor.cpp`
- `coprocessor/MRCoprocessorDispatch.cpp`
- `coprocessor/MRPerformance.cpp`
- deferred macro UI playback paths.

## Authority

The coprocessor owns task queues and lane state.

Deferred macro UI playback owns its local queue and screen-model transition state.

## Invariants

- Do not change thread/UI-thread assumptions incidentally.
- Do not change ownership/lifetime behavior incidentally.
- Do not alter deferred UI command flow as cleanup.
- Do not remove gateway/view chain elements if regression checks encode them as structure contract.
- Do not change batch boundaries without a dedicated plan.

## Protected structure

Some routing types may appear mechanical but are protected by regression checks.
If a type is referenced by regression structure checks, removing it is not a local readability change.

## Forbidden without explicit approval

- Changing deferred playback order.
- Changing command queue ownership.
- Changing `mrvmUiBeginMacroScreenBatch` / `mrvmUiEndMacroScreenBatch` boundaries.
- Replacing staged UI commands with direct UI calls.
- Moving rendering across thread assumptions.

## Required tests

For coprocessor/deferred UI changes, test:

- background task completion,
- deferred macro playback,
- batching,
- message-line effects,
- screen overlay projection,
- relevant regression checks.
