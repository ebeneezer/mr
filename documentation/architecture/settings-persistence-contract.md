# Settings Persistence Contract

## Scope

Applies to:

- `writeSettingsMacroFile`
- `persistConfiguredSettingsSnapshot`
- `buildSettingsMacroSource`
- `buildSettingsMacroSourceWithWorkspace`
- VM `SAVE_SETTINGS`
- settings save paths in App and dialogs.

## Authority

Persistence writes the current runtime settings model to `settings.mrmac`.

Persistence writes the current build's `MR_BUILD_EPOCH` as the canonical
settings-file version.

Persistence must not invent a second source of truth.

## Contract

Persistence is separate from bootstrap application.

Saving settings must not silently reload settings.
Saving settings must not rebuild runtime state from the file.
Saving settings must not use file merge semantics as authority.
Saving settings must not preserve an older persisted version marker.

## Theme

Theme persistence is currently coupled to settings writing in specific paths.
Do not move or split theme writing without an explicit theme/persistence decision.

When persistence writes external theme files, it must write the current
`MR_BUILD_EPOCH` as the file version.

## Workspace

Workspace serialization is a separate extension to the settings source, not a license to add arbitrary serialization side channels.

The debugger Bento extension may serialize cold source identity, breakpoint
definitions and watch definitions through the existing `WORKSPACE` line.
Session ids, VM handles, routes, current locations, call stacks, values,
outputs and generated source maps remain runtime-only and must not be written.

## SAVE_SETTINGS

`SAVE_SETTINGS` is currently a VM intrinsic that persists settings.
It is a protected boundary. Do not modify it as incidental cleanup.

## Forbidden without explicit approval

- New persistence entry points.
- Save/reload cycles.
- Duplicate writes to `settings.mrmac`.
- Moving theme write behavior.
- Moving workspace serialization.
- Persisting dialog buffers directly.
- Changing dirty clear semantics.
- Preserving stale persisted version numbers on write.

## Required tests

For persistence changes, test:

- save without changes,
- save after one setting change,
- save after theme change,
- save with workspace state,
- save via VM `SAVE_SETTINGS`,
- restart after save,
- invalid path or write failure.
