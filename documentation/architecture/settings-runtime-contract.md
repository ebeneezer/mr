# Settings Runtime Contract

## Scope

Applies to:

- `config/MRDialogPaths.cpp`
- `config/MRDialogPaths.hpp`
- `config/MRSettingsLoader.cpp`
- settings-related paths in `app/MREditorApp.cpp`
- settings-related VM intrinsics in `mrmac/MRVM.cpp`.

## Authority

There is one authoritative runtime settings model: the central in-memory model in the settings configuration layer.

`settings.mrmac` is serialization and bootstrap input.
It is not the runtime authority.

## Invariants

- No shadow settings stores.
- No parallel key/value registries.
- No save/reload workaround loops.
- No duplicate ownership for the same setting.
- Unknown/obsolete keys are not semantically carried forward.
- Missing current keys are supplied from defaults.
- Settings keys must have one canonical spelling and one canonical meaning.

## MRSETUP

`MRSETUP(...)` is the serialized settings transport and executable startup semantic inside the VM.

MRSETUP keys must not be renamed, repurposed or made ambiguous without an explicit migration decision.

## Runtime model

Resetting the settings model establishes defaults.
Applying settings overwrites defaults.
Serialization writes the current model.

## Forbidden without explicit approval

- Adding a second runtime model.
- Treating a dialog buffer as authoritative.
- Persisting from an unapproved code path.
- Reconstructing the runtime model by file merging.
- Reintroducing obsolete semantic keys.

## Required tests

For runtime settings changes, test:

- fresh default startup,
- partial settings file,
- obsolete keys,
- unknown keys,
- duplicate keys,
- save and restart,
- dirty-state behavior.
