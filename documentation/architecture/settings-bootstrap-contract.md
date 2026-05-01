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

The VM is the final startup apply actor.

The loader may stage, verify and canonicalize settings. It may currently use the settings model as a working medium for this staging pass. That staging state is not the final runtime authority.

The final authoritative startup application occurs only after the canonical source has been compiled and executed by the VM in startup MRSETUP mode.

After successful VM application, the central in-memory settings model is authoritative.

## Stages

The intended conceptual sequence is:

1. ensure settings file exists,
2. read source,
3. verify and normalize source,
4. drop obsolete/unknown entries as specified,
5. derive canonical source,
6. optionally rewrite canonical source,
7. reset runtime settings model,
8. compile canonical source,
9. execute VM in startup settings mode,
10. perform explicit post-apply steps,
11. clear dirty state,
12. mark runtime model authoritative.

## Current transitional rule

A staging function may internally touch the settings model if current code requires that.
This must not be described as final apply.

Do not “simplify” the bootstrap by collapsing staging and final VM application into one generic load helper.

## Theme and keymap

Theme and keymap behavior are separate contracts.
Do not move theme or keymap application as part of bootstrap cleanup unless the task explicitly targets that contract.

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
