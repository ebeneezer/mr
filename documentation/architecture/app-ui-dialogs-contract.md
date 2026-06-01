# App / UI / Dialogs Contract

## Scope

Applies to:

- `app/MREditorApp.cpp`
- `app/MRCommandRouter.cpp`
- `app/MRMenuFactory.cpp`
- `dialogs/MRSetup.cpp`
- `dialogs/MRWindowList.cpp`
- `dialogs/MRKeymapManager.cpp`
- `dialogs/MRMacroFile.cpp`
- `dialogs/MRCompilerProfiles.cpp`

## Authority

The UI is not authoritative for settings.

Dialogs may hold:

- input buffers,
- temporary selection state,
- TVision view state,
- validation state.

Authoritative settings state belongs to the settings runtime model.

Window/editor state belongs to the App/UI layer.

## Data flow

User event -> TVision event -> App / CommandRouter -> Dialog or Command -> domain-specific path.

Dialogs may trigger:

- settings updates,
- history updates,
- keymap loading,
- workspace operations,
- message-line output.

## Invariants

- Dialogs must remain TVision-native.
- Dialogs must not create shadow settings stores.
- Dialogs must not persist settings through ad hoc paths.
- Dialog validators must not display blocking error dialogs.
- Message-line or marquee feedback is preferred for validation warnings.
- Dirty state must be set only for real changes.
- Dialogs whose fixed layout may exceed the current terminal size must use the MR scrollable dialog path (`MRScrollableDialog` / `MRDialogFoundation`) or provide an explicit local proof that all controls remain reachable at smaller terminal sizes.

## Dialog layout design rules

These rules apply to new dialogs and to layout changes in existing dialogs.

- Radio-button clusters must have a left-aligned heading ending with a colon.
- Checkbox options that belong together must be grouped into one checkbox cluster with a left-aligned heading ending with a colon.
- Dialog content must keep equal visual distance to the left and right dialog borders.
- Dialog titles must be uppercase and must not use ellipses.
- Dialog button rows must be horizontally centered.
- Dialogs must not add a Cancel button by default. Closing without applying changes is handled by the dialog close action.
- Dialogs that can change settings must use clean dirty gating. Applying unchanged data must not mark settings dirty or trigger save prompts.
- Dialogs whose fixed layout may not fit the current terminal must switch cleanly into the scrollable dialog path so that all controls remain reachable.
- Options inside radio-button and checkbox clusters must leave at least one trailing space inside the highlighted cluster area after the longest visible option text.
- Neighboring clusters must keep two columns of horizontal spacing.
- Stacked widgets must keep one empty row of vertical spacing unless they form one logical multi-line control.
- Button rows must keep one empty row of spacing to the dialog frame.

## Allowed

- Local UI layout fixes.
- Validator improvements with unchanged semantics.
- Removal of dead UI scaffolding after call-site proof.
- Explicit local code when it improves readability.

## Forbidden without explicit approval

- New dialog architecture.
- New generic setup framework.
- New local settings registry.
- Overlay hacks.
- Direct screen-buffer manipulation.
- Opportunistic changes to history behavior.
- Changes to keymap, workspace or settings persistence from dialog code.

## Deferred design note

- `ui/MRDropList.*` may become a shared visual primitive for small string-selection popups.
- Reuse for file/path history in load/save dialogs is not implicitly approved by its first use in file-extension settings.
- A dedicated history tranche is required before reusing it there.
- That tranche must explicitly review:
  - cancel on outside click,
  - integration with the existing scoped history button/popup flow,
  - focus and event behavior inside load/save and directory dialogs.
- Until such a tranche is approved, the code-language drop list and scoped history popups may remain separate.

## Required tests

For dialog changes, test:

- open dialog,
- initial validation,
- invalid input,
- valid input,
- save/apply path,
- cancel path,
- terminal-size constraints when relevant, including reachability of all interactive controls for fixed-size setup dialogs below nominal width or height.
