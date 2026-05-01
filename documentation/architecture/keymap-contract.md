# Keymap Contract

## Scope

Applies to:

- `keymap/MRKeymapProfile.hpp`
- `keymap/MRKeymapProfile.cpp`
- `keymap/MRKeymapResolver.cpp`
- `keymap/MRKeymapTrie.cpp`
- `keymap/MRKeymapActionCatalog.cpp`
- `dialogs/MRKeymapManager.cpp`
- keymap-related settings loader and VM paths.

## Authority

Configured keymap profiles and the active profile belong to the settings runtime model.

The resolver is a runtime projection of that model.

## Data flow

Settings source -> keymap parse/canonicalize -> settings model -> resolver -> runtime key handling.

## Invariants

- `DEFAULT` fallback semantics must remain explicit.
- Active profile selection must remain deliberate.
- Keymap persistence format must not change incidentally.
- Resolver rebuild semantics must not change as formatting cleanup.
- Diagnostics texts and severity must not change without approval.
- Loader and dialog roles must not be merged casually.

## Bootstrap relation

Keymap currently has loader-side staging/canonicalization behavior.
Whether loader-side keymap application remains tolerated or becomes purely staging is a protected decision.

Do not change this while working on unrelated settings or UI tasks.

## Forbidden without explicit approval

- Changing `KEYMAP_PROFILE`, `KEYMAP_BIND`, `ACTIVE_KEYMAP_PROFILE` semantics.
- Changing sequence conflict handling.
- Changing resolver lookup/fallback behavior.
- Moving diagnostics to a shared API without a dedicated plan.
- Changing persisted keymap ordering or canonicalization.

## Required tests

For keymap changes, test:

- load default profile,
- load active profile,
- missing active profile fallback,
- duplicate/conflicting bindings,
- dialog load/save,
- resolver rebuild,
- startup with invalid keymap settings,
- persistence and restart.
