# Settings Bootstrap Contract

## Scope

Protected functions include:

- `loadStartupSettingsMacro`
- `buildCanonicalSettingsSource`
- `normalizeSettingsMacroToCurrentModel`
- `applySettingsSourceViaVm`
- `loadAndNormalizeSettingsSource`
- `resetConfiguredSettingsModel`
- `applyConfiguredSettingsAssignment`
- VM `MRSETUP`
- VM `SAVE_SETTINGS`
- `buildSettingsMacroSourceWithWorkspace`

## Contract

The settings bootstrap is VM-centered.

`settings.mrmac` is a verified and canonicalized macro source.

The bootstrap owns the valid version of the settings source.

An older or partial settings file is input to bootstrap normalization, not an
authority over the accepted settings version.

Known settings from older sources are carried forward by meaning into the
current model where still valid.

Unknown or obsolete settings are dropped during normalization.

Missing settings required by the current model are supplied from current
defaults.

The bootstrap derives the current canonical settings source from that complete
normalized model.

When canonical rewrite is performed, the rewritten source must use the current
canonical settings version rather than preserve the older source version.

The VM is the final startup apply actor.

The loader may stage, verify and canonicalize settings. It may currently use the settings model as a working medium for this staging pass. That staging state is not the final runtime authority.

The final authoritative startup application occurs only after the canonical source has been compiled and executed by the VM in startup MRSETUP mode.

After successful VM application, the central in-memory settings model is authoritative.

## Stages

The intended conceptual sequence is:

1. ensure settings file exists,
2. read source,
3. verify and normalize source,
4. keep known still-valid settings and drop obsolete/unknown entries as specified,
5. supply missing current settings from defaults,
6. derive canonical current-version source,
7. optionally rewrite canonical source,
8. reset runtime settings model,
9. compile canonical source,
10. execute VM in startup settings mode,
11. perform explicit post-apply steps,
12. clear dirty state,
13. mark runtime model authoritative.

## Current transitional rule

A staging function may internally touch the settings model if current code requires that.
This must not be described as final apply.

Do not “simplify” the bootstrap by collapsing staging and final VM application into one generic load helper.

## Theme and keymap

Theme and keymap behavior are separate contracts.
Do not move theme or keymap application as part of bootstrap cleanup unless the task explicitly targets that contract.

## Transitional keymap exception

Keymap is currently a tolerated staging exception inside the VM-centered bootstrap contract.

The loader may canonicalize keymap data and write that canonicalized result into the runtime settings model before the final VM apply.

This exception exists because `buildSettingsMacroSource(...)` currently serializes keymap data from `configuredKeymapProfiles()` and `configuredActiveKeymapProfile()`.

That loader-side keymap write is not the final authoritative runtime state.

The final authoritative keymap state still arises only after the canonical source has been compiled and executed by the VM in startup mode.

Do not extend this exception to other settings areas without an explicit contract decision.

## SAVE_SETTINGS

`SAVE_SETTINGS` is not part of the bootstrap cleanup contract.
Do not move or rewrite it incidentally.

## Workspace

Workspace serialization is not part of the canonical settings core unless a separate workspace contract change is approved.

## Forbidden without explicit approval

- Removing final VM startup apply.
- Applying canonical settings only through the loader.
- Moving `MRSETUP` out of the VM.
- Moving `SAVE_SETTINGS` as part of bootstrap cleanup.
- Changing startup gating for `MRSETUP`.
- Changing key meanings during bootstrap refactoring.
- Changing dirty-state behavior without a dedicated plan.
- Moving workspace lines into the canonical core.

## Required checks

For bootstrap changes, run:

- `make clean all CXX=clang++`,
- regression checks,
- startup with empty settings file,
- startup with partial settings file,
- startup with non-canonical but valid settings file,
- startup with obsolete/unknown keys,
- theme-related startup probe,
- keymap-related startup probe,
- save/restart probe.
