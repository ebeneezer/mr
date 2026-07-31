# Keymap Contract

## Scope

Applies to:

- `keymap/`,
- `dialogs/MRKeymapManager.*`,
- keymap source handling under `config/settings/`,
- `mrmac/vm/MRVMKeymapRuntime.*`,
- runtime key dispatch.

## Authority

Configured keymap profiles and the selected active profile belong to the
settings runtime model. The resolver and trie are runtime projections of that
model. External keymap files are versioned persistence artifacts.

The running build's `MR_BUILD_EPOCH` is the canonical persisted keymap version.

## Data flow

Settings source -> parse and canonicalize -> settings model -> resolver and
trie -> runtime key handling.

## Invariants

- No active or invalid runtime keymap falls through to built-in command
  handling.
- Active profile selection is deliberate and is not inferred from resolver
  failure.
- The active profile's `DEFAULT` behavior and invalid-profile fallback remain
  stable.
- Sequence conflict handling, canonical ordering and resolver rebuild
  semantics are part of the keymap contract.
- Diagnostic text and severity are observable UI behavior.
- Loader, dialog and resolver roles remain distinct.
- An older external version is upgrade input; a newer external version is
  invalid.
- Writing an external keymap emits the running build epoch.

## Bootstrap relation

The loader may stage validated, canonical keymap data only as described by the
[Settings Bootstrap contract](settings-bootstrap-contract.md). Staging is not
the final authoritative resolver state; VM startup application remains the
final apply path.

## Boundaries

Without explicit maintainer approval:

- Do not change `KEYMAP_PROFILE`, `KEYMAP_BIND` or
  `ACTIVE_KEYMAP_PROFILE` semantics.
- Do not alter lookup fallback, sequence conflicts, persisted ordering,
  canonicalization or diagnostic behavior incidentally.
- Do not merge loader and dialog format ownership or create a second keymap
  registry.

## Related contracts

- [Settings Runtime](settings-runtime-contract.md)
- [Settings Bootstrap](settings-bootstrap-contract.md)
- [App / UI / Dialogs](app-ui-dialogs-contract.md)

## Required manual tests

- Load a valid active profile.
- Probe missing and invalid active-profile fallback.
- Load duplicate and conflicting bindings.
- Load, edit and save through the dialog.
- Rebuild the resolver and switch the active profile.
- Start with invalid keymap settings.
- Persist, restart and verify version upgrade/rejection behavior.
