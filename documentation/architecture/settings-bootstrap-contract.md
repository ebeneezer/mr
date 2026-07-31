# Settings Bootstrap Contract

## Scope

Protected paths include:

- `app/MREditorApp.cpp`: `loadStartupSettingsMacro` and
  `applySettingsSourceViaVm`,
- `config/settings/MRSettingsNormalize.*`,
- `config/settings/MRSettingsSnapshotIO.*`,
- `buildCanonicalSettingsSource`,
- `prepareStartupSettingsSource`,
- `loadAndNormalizeSettingsSource`,
- `resetConfiguredSettingsModel`,
- `applyConfiguredSettingsAssignment`,
- VM `MRSETUP` startup handling.

## Authority

Bootstrap owns the accepted, canonical current-version source used at startup.
The VM is the final startup-apply actor. The central runtime settings model
becomes authoritative only after canonical source compiles and executes
successfully in startup settings mode.

The canonical persistence version is the running build's `MR_BUILD_EPOCH`.

## Input normalization

- A lower persisted version is upgrade input.
- A higher persisted version is rejected as future input.
- Current known keys retain their canonical meaning.
- Missing current keys receive current defaults.
- Unknown, obsolete and retired keys are dropped silently.
- A retired-token compatibility list or accepted no-op spelling must not be
  introduced.
- Canonical source is generated from the complete normalized current model and
  carries the current build epoch.
- The final VM apply receives only canonical current-source settings.

## Bootstrap sequence

1. Ensure the settings file exists.
2. Read the source.
3. Parse, validate and normalize into staging state.
4. Retain valid current meanings and supply missing defaults.
5. Generate canonical current-version source.
6. Rewrite that source when normalization requires it.
7. Reset the runtime settings model.
8. Compile and execute canonical source in VM startup settings mode.
9. Perform explicit post-apply work.
10. Clear dirty state and expose the runtime model as authoritative.

Staging, canonicalization and final apply must remain visible as distinct
roles. A staging implementation may use the runtime model as a temporary
working medium only where the current code requires it; that write is not
final apply.

## Transitional keymap staging

The loader may validate and canonicalize keymap input and stage that result in
the runtime settings model because canonical source generation currently reads
the configured keymap projection. Final authoritative keymap application still
occurs through VM startup execution.

This exception is limited to keymap staging and must not spread to another
settings domain.

## AUTOEXEC macros

`AUTOEXEC_MACRO` entries are configured selection. Bootstrap attempts every
configured entry but does not invent entries.

A missing or non-executable entry is logged, removed from the configured list,
marks settings dirty and is persisted through the existing coalesced flush.
This cleanup does not use the message line. A successfully executable entry
remains configured.

Here, silently means that bootstrap continues without a modal error, failed
startup or invented compatibility behavior. The drop is recorded in the
normal bootstrap log/load report so that configuration loss remains
diagnosable.

The current build supplies hardcoded defaults for every setting token it knows.
Known tokens from the settings source may overwrite those defaults. Missing
known tokens keep the current build defaults.

## Workspace

Workspace serialization remains outside the canonical settings core. The
approved debugger extension stores only cold debugger Bento configuration in
`WORKSPACE`: source path and macro identity, breakpoint definitions and watch
definitions. It does not serialize a debugger session, VM state, generated
source map or bytecode offset.

Restore validates breakpoint bindings against a newly generated canonical
source map and validates watches with the canonical restricted expression
compiler. Entries that cannot be bound or compiled are dropped and logged;
they do not remain as pending debugger state and do not prevent restoration of
unrelated valid fields.

## Boundaries

Without explicit maintainer approval:

- Do not remove or bypass final VM startup apply.
- Do not move `MRSETUP` out of the VM or alter its startup gating here.
- Do not change key meanings or dirty-state semantics as bootstrap cleanup.
- `SAVE_SETTINGS`, theme persistence and workspace serialization remain outside
  bootstrap ownership.

## Related contracts

- [Settings Runtime](settings-runtime-contract.md)
- [Settings Persistence](settings-persistence-contract.md)
- [Keymap](keymap-contract.md)
- [VM / Intrinsics / Deferred UI](vm-deferred-ui-contract.md)

## Required manual tests

- Start with an absent, empty and partial settings file.
- Start with valid older, current and future-version input.
- Start with unknown, obsolete, duplicate and invalid assignments.
- Verify final VM application, dirty clearing and canonical rewrite.
- Probe theme, keymap and AUTOEXEC post-normalization behavior.
- Save, restart and compare the resulting runtime settings.
