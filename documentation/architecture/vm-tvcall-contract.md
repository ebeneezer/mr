# VM / Intrinsics / TVCALL Contract

## Scope

Applies to:

- `mrmac/MRVM.cpp`
- `mrmac/MRVM.hpp`
- VM intrinsic dispatch,
- `MRSETUP`,
- `SAVE_SETTINGS`,
- `OP_TVCALL`,
- MacroCellGrid / MacroCellView / UI facade bridges.

## Authority

The VM owns bytecode execution state.

For screen operations, the VM is a producer of staged UI commands and facade mutations.
TVision-facing UI code is the consumer/projection layer.

## Invariants

- Do not create competing screen write paths.
- Do not bypass MacroCellGrid / MacroCellView contracts.
- Do not blur producer/consumer boundaries.
- Do not modify `MRSETUP` startup gating incidentally.
- Do not modify `SAVE_SETTINGS` incidentally.
- Do not change VM error texts without regression awareness.

## MRSETUP

`MRSETUP` is allowed in controlled startup settings mode.
Runtime use restrictions are part of VM semantics.

## TVCALL

TVCALL must not directly become a TVision screen hack.
Screen operations must remain staged/projection-based unless a dedicated architecture decision says otherwise.

## Forbidden without explicit approval

- Direct render facade usage outside whitelisted bridge points.
- New VM persistence side effects.
- New UI side effects hidden in generic intrinsics.
- Changing startup mode conditions.
- Changing deferred UI batching boundaries.

## Required tests

For VM/TVCALL changes, test:

- clean build,
- VM regression checks,
- MRSETUP allowed and forbidden contexts,
- SAVE_SETTINGS behavior if touched,
- TVCALL macro cases,
- deferred playback if touched,
- UI facade projection behavior.
