# TVision Integration Contract

## Scope

Applies to:

- `dialogs/MRSetupCommon.hpp`
- `dialogs/MRSetup.cpp`
- `ui/MRFrame.cpp`
- `ui/MRWindowSupport.cpp`
- `ui/MRMessageLineController.cpp`
- `ui/widgets/MRColumnListView.cpp`
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

## Allowed

- TVision-compliant overrides.
- Dialog layout corrections.
- Focus handling through documented TVision mechanisms.
- Message-line control through MR message-line controllers.

## Forbidden without explicit approval

- Direct writes to `TScreen::screenBuffer`.
- New render side channels.
- Event interception used to simulate read-only or disabled state.
- View-lifetime changes outside local ownership proof.
- Changes to `MRDialogFoundation` behavior as incidental cleanup.

## Required tests

For TVision-related changes, test:

- focus traversal,
- drawing after expose/resize,
- modal dialog open/close,
- disabled/ghosted controls,
- scroll behavior where relevant.

## Dirty Projection Pump Contract

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

## Window minimize/restore rendering contract

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
