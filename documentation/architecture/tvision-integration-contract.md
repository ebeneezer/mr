# TVision Integration Contract

## Scope

Applies to:

- `dialogs/setup/MRSetupCommon.*`
- `dialogs/setup/MRSetup*.cpp`
- `ui/MRFrame.cpp`
- `ui/MRWindowSupport.cpp`
- `ui/MRMessageLineController.cpp`
- `ui/widgets/MRColumnListView.cpp`
- `ui/MRBentoBox/`
- `ui/MRBentoHexEditor/`
- all MR classes derived from TVision views.

## Authority

TVision owns:

- focus,
- Z-order,
- event routing,
- view lifetime,
- draw dispatch.

MR may extend TVision through standard TVision mechanisms only.

## Invariants

- Do not modify files under `tvision/`.
- Do not bypass `TView` / `TDrawBuffer` / dialog mechanisms.
- Do not introduce direct screen-buffer writes.
- Do not use overlay hacks when a TVision-native route exists.
- Do not change event routing as a side effect of layout cleanup.

## Message-line Static Mode

The authoritative Static Mode semaphore is the central runtime K/V value
`APPLICATIONUI/messageLine/staticMode`.

- Entering Static Mode clears every ordinary message-line owner slot.
- Ordinary messages arriving while it is active are discarded rather than
  queued.
- The menu bar may retain only the current numeric progress projection.
- Progress uses the dynamically derived message lane, Warning color and a
  centered `completed/total` label without marquee transitions.
- All progress-bar consumers use the common `MRProgressSlider` renderer.
  Dialog and MMP projections retain their left-to-right direction; Static Mode
  fills the message lane from right to left.
- Leaving Static Mode clears the projected content before ordinary animated
  messages resume.
- Entry and exit use the existing function-key label transitions. Entry
  animates the previous labels out and `Esc Abort` in; exit reverses that
  transition and restores the captured pre-entry presentation.
- F1 through F12 cease dispatch as soon as Static Mode is active, including
  while their outgoing labels remain visible. After the entry transition the
  status line shows only `Esc Abort`. Escape remains owned by the active
  operation.

## Message-line normal mode

- A message enters from the right through the normal marquee intro.
- After the intro has completed, the fully visible message remains stable for
  at least two seconds before a pending replacement may start the outro.
- The presentation retains at most one pending replacement. A later non-empty
  replacement may coalesce it; no unbounded message queue is introduced.

## Desktop-managed windows

Top-level windows that participate in MR desktop operations implement the
`MRDesktopWindow` role. It is the common polymorphic boundary for focusable,
tileable desktop windows; window commands must not branch on MMP, editor or
BentoBox concrete types.

- The role exposes native TVision projection, virtual-desktop membership,
  manual visibility, shared minimize/restore capability and geometry application.
- Editor-specific actions such as save, revert, file comparison and workspace
  ownership remain capabilities of `MREditWindow`; they are not desktop-window
  operations.
- A modeless MRMAC window participates through the same role while its
  macro-visible value state remains under `MODELESSUI`.
- Modal dialogs executed through `execView` are not desktop-managed windows.

## MRMAC desktop background canvas

The MRMAC desktop canvas is retained runtime state projected by
`MRDesktopBackground::draw()` through normal `TDrawBuffer` and `TView`
mechanisms.

- The canvas is clipped to the desktop background's local bounds.
- Desktop windows remain above the canvas through normal TVision ownership and
  Z-order.
- The menu bar and FLabel/status line are outside the canvas projection bounds
  and must not be overwritten.
- The virtual-desktop marker is projected after the canvas and remains visible.
- Expose, resize and virtual-desktop changes redraw from the retained central
  VM K/V state; the background keeps no value-bearing canvas mirror.
- Clearing the canvas redraws the native desktop pattern in the vacated cells
  while preserving the MRMAC desktop drawing attribute.
- ANSI files are decoded before projection. In colour mode, standard ANSI
  16-colour SGR values map directly to TVision/BIOS attributes. In
  desktop-monochrome mode, the VM producer maps the 16 ANSI colours to stable
  luminance-ordered monochrome ranks. It converts equal upper/lower samples
  through the CP437 shade ramp and preserves unequal samples as two luminance
  planes represented by ordered light-shade, half-block and full-block output.
  It must not average unequal samples. Ordinary glyphs remain ordinary glyphs.
  The projection layer must not quantise, dither or reinterpret either payload.
- Only the typed MRMAC VM/facade path may mutate the canvas.
- The canvas must not write `TScreen::screenBuffer` directly.

## Boundaries

Without explicit maintainer approval:

- Direct writes to `TScreen::screenBuffer`.
- New render side channels.
- Event interception used to simulate read-only or disabled state.
- View-lifetime changes outside local ownership proof.
- Changes to `MRDialogFoundation` behavior as incidental cleanup.

## Related contracts

- [App / UI / Dialogs](app-ui-dialogs-contract.md)
- [Hex Editor](hex-editor-contract.md)
- [VM / Intrinsics / Deferred UI](vm-deferred-ui-contract.md)

## Required manual tests

For TVision-related changes, test:

- focus traversal,
- drawing after expose/resize,
- modal dialog open/close,
- disabled/ghosted controls,
- scroll behavior where relevant.
- MRMAC desktop-canvas clipping, window occlusion, virtual-desktop marker,
  menu-bar preservation and FLabel/status-line preservation where relevant.
- MRMAC desktop-canvas clear and subsequent attribute reuse where relevant.

## Dirty projection pump

Dirty projection is an explicit UI redraw scheduling mechanism for compound
TVision views such as BentoBox. It is not model state.

Rules:

1. Dirty bits describe pending projection work only: content, chrome,
   scrollbar, layout and overlay.
2. Event handlers set dirty bits and flush them at controlled exit points.
3. Full-layout paths may draw directly, but must consume all dirty bits before
   returning.
4. A layout dirty bit dominates narrower projection work. Layout is allowed to
   re-project content, chrome and scrollbars as part of its normal TVision
   layout path.
5. Dirty projection must not change TVision ownership, focus routing, Z-order
   or view lifetime.
6. Dirty bits must not be used as hidden state transitions. If a semantic state
   changes, store it in the owning model first and mark projection dirty after
   that.
7. Do not introduce a shared dirty-pump base class until at least two widgets
   have the same proven projection contract. Local explicit code is preferred
   over a premature framework.

## Window minimize/restore rendering

Minimize/restore is a visual-form transition, not ordinary move/resize.

For transitions between a normal editor window and its minimized one-line
representation:

1. Hide the currently visible form first.
2. Do not mutate bounds, minimized state, frame bounds or shadow state before
   the old form has been hidden with its original geometry and shadow state.
3. While hidden, update the new state and bounds with non-drawing geometry
   assignment.
4. Do not use visible `locate()` or `changeBounds()` for the full-window <->
   minimized-icon transition.
5. The minimized representation must not cast a normal window shadow.
6. If the hidden full-size window may have cast a shadow into the dock row used
   by the minimized representation, a single targeted `TScreen::flushScreen()`
   immediately after `hide()` is acceptable as a render barrier.
7. Do not use broad desktop redraw, reload, or watchdog-style repaint logic as
   the final fix.
