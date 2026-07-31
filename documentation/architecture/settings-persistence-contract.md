# Settings Persistence Contract

## Scope

Applies to:

- `config/settings/MRSettingsStorage.*`,
- persistence paths in `config/settings/MRSettingsRuntime.cpp`,
- source generation in `config/settings/MRSettingsSnapshotIO.*`,
- `buildSettingsMacroSource`,
- `persistConfiguredSettingsSnapshot`,
- `persistConfiguredSettingsSnapshotWithWorkspace`,
- `writeSettingsMacroFile`,
- `buildSettingsMacroSourceWithWorkspace`,
- VM `SAVE_SETTINGS` and App/dialog save call sites.

## Authority

Persistence serializes a snapshot of the authoritative runtime settings model
to `settings.mrmac`. It does not own runtime values and must not invent a
second source of truth.

Canonical settings and external theme artifacts carry the running build's
`MR_BUILD_EPOCH`.

## Invariants

- Saving is separate from bootstrap and runtime application.
- A save neither reloads settings nor rebuilds runtime state from a file.
- Canonical settings source is generated from the current runtime snapshot,
  not through file-merge authority.
- A successful write clears dirty state only according to the existing
  persistence path's contract.
- Failed or partial writes must not be reported as successful persistence.
- Existing theme-file coupling remains explicit and versioned.

## Workspace extension

`WORKSPACE` lines are a protected extension carried in `settings.mrmac`; they
are not part of the canonical settings core.

Normal settings persistence preserves the existing workspace extension.
Workspace-aware persistence may replace it only through
`buildSettingsMacroSourceWithWorkspace`. This exception does not authorize
another serialization side channel.

## SAVE_SETTINGS

`SAVE_SETTINGS` is a VM intrinsic that invokes the approved persistence path.
Its location, availability and error behavior are protected.

## Boundaries

Without explicit maintainer approval:

- No new settings writer or duplicate `settings.mrmac` write path.
- No save/reload cycle or direct dialog-buffer serialization.
- No stale persisted version marker.
- No incidental movement of theme writes, workspace serialization or dirty
  clearing.

## Related contracts

- [Settings Runtime](settings-runtime-contract.md)
- [Settings Bootstrap](settings-bootstrap-contract.md)
- [VM / Intrinsics / Deferred UI](vm-deferred-ui-contract.md)

## Required manual tests

- Save with and without a settings change.
- Save after a theme change and inspect artifact versions.
- Save while preserving existing workspace lines.
- Save a fresh workspace snapshot and restore it.
- Save through VM `SAVE_SETTINGS`.
- Restart after each write path.
- Exercise invalid path and write-failure handling.
