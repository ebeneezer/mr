# Hex Editor Contract

## Scope

Applies to:

- `ui/MRBentoHexEditor/`,
- Hex-specific extensions of `ui/MRBentoBox/`,
- Hex editor document, save and workspace integration.

## Authority

The existing editor document and AddBuffer own the canonical byte sequence.
`MRBentoHexEditor` owns the canonical byte cursor and Hex-specific display
configuration. `MRBentoBox` owns generic pane layout, lifetime, focus and
event routing.

Each `MRHexPaneWindow` hosts one Hex role. Its `MRHexPaneView` projects an
immutable, versioned view of the shared document.

## Invariants

- Hex, Strings, Inspector, Decimal, Binary and Octal are equivalent pane-host
  roles; the primary leaf is not a rendering exception.
- Generic Bento code must not branch on Hex modes, roles or number bases.
- Pane views draw only within their local TVision extent and never repaint
  neighboring panes or Bento chrome.
- Hex projection work runs from immutable document snapshots. Adoption must
  match document identity and version, pane role, cursor projection revision,
  endianness, record length, viewport and pane geometry.
- `draw()` consumes an adopted projection. It must not perform string
  detection, inspector conversion or complete numeric projection.
- Editing uses existing document insert, erase and replace operations. There
  is no second byte buffer, pane-local document or competing cursor truth.
- Byte-exact save mode is local to the Hex document. It must not alter
  `FILE_TYPE`, settings, profiles or settings persistence.
- Workspace restore may reconstruct stable Hex and Bento state. It must not
  restore worker, task, projection or runtime owner identities.

## Related contracts

- [TVision Integration](tvision-integration-contract.md)
- [Coprocessor Runtime](coprocessor-runtime-contract.md)
- [Settings Persistence](settings-persistence-contract.md)

## Required manual tests

- Open and close a Hex editor with all six panes.
- Exercise focus, pane switching, splitters, resize and scrolling.
- Edit through every writable pane; verify commit, `Esc`, Insert and Overwrite.
- Save and reload byte-identical binary data.
- Change record length and endianness and verify fresh projections.
- Save and restore a Hex workspace without restored runtime identities.
