# Settings Runtime Contract

## Scope

Applies to:

- `config/settings/MRSettingsRuntime.*`,
- `config/settings/MRSettingsRuntimeState.*`,
- `config/settings/MRSettingsAssignments.*`,
- domain-specific settings accessors under `config/settings/`,
- settings-related App, dialog and VM call sites.

## Authority

The central VM K/V store owns the authoritative in-memory settings model under
`SETTINGS/runtime`. Dialog buffers, parsed source models and
`MRSettingsSnapshot` values are input or transfer state, not competing runtime
authorities.

Dialog histories belong under `SETTINGS/history`. Bootstrap working state
belongs under `SETTINGS/staging`; neither branch may mirror authoritative
values after its operation has ended.

`settings.mrmac` is bootstrap input and serialization. It is not runtime state.

## Invariants

- Reset establishes the complete current defaults.
- Applying an accepted assignment changes `SETTINGS/runtime`.
- One setting has one owner, one canonical spelling and one canonical meaning.
- Dirty state changes only when the authoritative value changes.
- Runtime consumers read the central model; they do not reconstruct it from
  persisted source.
- Staging, C++ snapshots and accessor return values must not become shadow
  settings stores.
- Keymap, theme and workspace state keep the ownership defined by their
  respective contracts.

Input versioning, normalization and final VM apply belong to the
[Settings Bootstrap contract](settings-bootstrap-contract.md). Source
generation and file writes belong to the
[Settings Persistence contract](settings-persistence-contract.md).

## Boundaries

Without explicit maintainer approval:

- No second runtime settings model or parallel settings registry.
- No save/reload loop used to apply runtime state.
- No dialog-owned or file-merge authority.
- No persistence from an unapproved call path.

## Related contracts

- [Settings Bootstrap](settings-bootstrap-contract.md)
- [Settings Persistence](settings-persistence-contract.md)
- [Keymap](keymap-contract.md)

## Required manual tests

- Reset to current defaults.
- Apply one valid assignment and reject one invalid assignment.
- Apply the same value twice and verify clean dirty gating.
- Read the changed value through its normal runtime consumer.
