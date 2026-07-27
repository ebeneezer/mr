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

The canonical persistence version is the current `MR_BUILD_EPOCH` of the
running build.

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

## Obsolete And Unknown Keys

The bootstrap must not keep a retired-token compatibility list.

Any `MRSETUP` token that is not known to the running build is obsolete or
unknown input. It must be dropped silently during bootstrap normalization and
must not be applied to the staging snapshot or final runtime model.

Here, silently means that bootstrap continues without a modal error, failed
startup or invented compatibility behavior. The drop is recorded in the
normal bootstrap log/load report so that configuration loss remains
diagnosable.

The current build supplies hardcoded defaults for every setting token it knows.
Known tokens from the settings source may overwrite those defaults. Missing
known tokens keep the current build defaults.

The final canonical settings source is generated only from the complete current
model. Therefore unknown, obsolete or retired tokens disappear because they are
not part of the current model, not because a save path filters a growing list of
legacy spellings.

Do not reintroduce removed settings as accepted no-op keys. If a token is no
longer canonical, it must become unknown to the bootstrap classifier.

The final VM startup apply must receive only the canonicalized current-source
settings. The VM may reject unknown `MRSETUP` keys; unknown and obsolete input
must be eliminated before the final VM apply.

Any persisted settings source with a version lower than the running
`MR_BUILD_EPOCH` is upgrade-required input.

Any persisted settings source with a version higher than the running
`MR_BUILD_EPOCH` must be rejected as a future-version source.

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

External theme and keymap files follow the same build-epoch version rule when
they are loaded through bootstrap-related paths.

## AUTOEXEC Macros

`AUTOEXEC_MACRO` entries are configured selection. Bootstrap does not derive,
add or otherwise decide which macros are marked for AUTOEXEC.

Bootstrap attempts every configured entry. A missing or non-executable entry is
logged and removed from the existing configured entry list. The mutation marks
the runtime settings dirty; the central coalesced settings flush persists the
filtered canonical source. This cleanup must not use the message line. A
successfully executable entry remains configured.

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

The approved debugger extension stores only cold debugger Bento configuration
in `WORKSPACE`: source identity/path, macro identity, breakpoint definitions
and watch definitions. It does not serialize a debugger session or VM state.
Unknown `WORKSPACE` option keys are dropped and logged without blocking the
remaining workspace entry; malformed values of known options are likewise
local failures rather than permission to reject unrelated known fields.

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
