# Coprocessor Runtime Contract

## Scope

Applies to:

- `coprocessor/MRCoprocessor.*`,
- worker lifecycle, affinity and telemetry,
- execution-owner and package metadata,
- external-source registration and shutdown,
- versioned result adoption by editor and pane consumers.

## Authority

An execution owner identifies a domain lifecycle such as an editor, pane,
macro session, dialog or external source. A task identifies one bounded work
package. `workerOrdinal` identifies one worker execution. A lane classifies
work; it is neither an owner nor a serialized lane-wide worker.

The domain consumer owns derived-state adoption. The coprocessor owns worker
lifetime, scheduling metadata, result delivery and cancellation signaling.

## Invariants

- Independent bounded packages receive independent workers. Work from
  different owners must not wait behind one lane-wide worker.
- One owner may have several concurrent workers for independent packages or
  directions.
- Core assignment is stable for a worker lifetime and uses strict round robin:
  `allowedCores[workerOrdinal % allowedCoreCount]`.
- Workers read immutable snapshots and never mutate TVision objects or
  canonical document state.
- Results carry enough owner, document, version, generation and span metadata
  for the domain consumer to reject stale adoption.
- UI-thread adoption is bounded. Drawing, event dispatch and TVision mutation
  remain on the UI thread; scan, analysis and expensive projection do not.
- Focus loss does not pause background work. Workspace restore creates new
  owners and workers; runtime identities are never serialized.
- Obsolete queued packages may be discarded. A running bounded package may
  finish, but an obsolete result must not overwrite a newer visible
  projection.
- A genuinely blocking external source may retain one exclusive worker for
  its source lifetime. EOF, close, cancellation, error and process termination
  must wake, stop, join and report that worker.
- A paused MRMac execution session does not retain a worker merely because the
  session remains alive.
- Worker creation, assignment, package state, result delivery, stopping and
  termination remain observable through coprocessor telemetry.
- Coprocessor worker lifetime remains explicit C++18-style `std::thread`,
  condition-variable and join logic.

## Related contracts

- [MRMac Execution Session](mrmac-exec-session-contract.md)
- [Coprocessor / Deferred UI](coprocessor-deferred-ui-contract.md)
- [Syntax Analysis](syntax-analysis-contract.md)
- [TVision Integration](tvision-integration-contract.md)

## Required manual tests

- Run independent work for multiple editor and pane owners concurrently.
- Verify strict round-robin affinity with more workers than allowed cores.
- Replace a generation while older work is running and reject stale adoption.
- Confirm background owners continue after focus changes.
- Close owners and external sources and verify complete worker termination.
- Restore a workspace and verify newly allocated runtime identities.
- Inspect telemetry for the complete worker and result lifecycle.
