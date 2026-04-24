# Option C: Direkte TV-Pfade hinter die Screen Facade ziehen

## Ausgangslage

Erledigt sind die verbindlichen Grundlagen:

- VM/Mailbox-Screen-Ops laufen ueber `MRMacroDeferredUiCommand`.
- `mrvmUiRenderFacadeRenderDeferredCommand()` ist der zentrale Einstieg in die VM/UI Render Facade.
- `MacroCellGrid`/`MacroCellView` rendern Macro-Screen-Overlays.
- `ScreenStateCoordinator` trennt Base-Invalidation und Overlay-Mutation.
- Der Coprocessor besitzt nur noch ein `DeferredUiRenderGateway`, keine zweite `UiRenderFacade`.
- Regression-Guards verhindern `TScreen::screenBuffer` im VM/Playback-Pfad.

Option C zieht nun kollisionsrelevante direkte TVision-Pfade schrittweise hinter dieselbe Facade. Normale `TView::draw()`-Implementierungen bleiben erlaubt. Ziel ist nicht, TVision zu ersetzen, sondern direkte Render-Sinks, die mit Macro-Overlays kollidieren koennen, ueber ein gemeinsames Invalidations- und Reprojection-Protokoll zu fuehren.

## Zugpaket C1: Render-Sink-Klassifikation

Ziel: Alle direkten Zeichenpfade fachlich einordnen, bevor Code umgebaut wird.

Umfang:

- `rg "writeLine|writeBuf|drawView|flushScreen|TScreen::" app ui dialogs mrmac coprocessor`
- Jede Fundstelle einer Kategorie zuordnen:
  - `ordinary-view-draw`: normale TVision View zeichnet ihren eigenen Inhalt.
  - `base-redraw-trigger`: zeichnet oder refreshes TVision-Base und kann Overlay-Reprojection brauchen.
  - `overlay-render`: MacroCellGrid/MacroCellView.
  - `unsafe-physical-write`: physischer Screenzugriff ausserhalb erlaubter Facade.
- Ergebnis als Kommentarblock oder Guard-Liste im Regressionstest festhalten, nicht als neue Doku.

Definition of Done:

- Liste der erlaubten Render-Sinks ist im Guard abgebildet.
- Neue direkte physische Writes fallen im Guard auf.
- Kein Funktionsumbau in diesem Paket.

## Zugpaket C2: Facade-API fuer Base-Invalidation

Ziel: Direkte UI-Operationen beruehren den `ScreenStateCoordinator` nicht mehr ad hoc, sondern ueber benannte Facade-Operationen.

Umfang:

- In `mrvm.cpp` die Facade um Base-Operationen erweitern:
  - `noteBaseMutation()`
  - `noteBaseRedraw()`
  - `renderBaseThenOverlayIfNeeded()`
  - `renderOverlay()`
- Bestehende Pfade `returnWithDirectScreenMutation()` und `returnWithMacroScreenMutation()` intern auf diese Facade-Methoden routen.
- `mrvmUiInvalidateScreenBase()` als oeffentliche API beibehalten, aber intern ebenfalls Facade-gesteuert.

Definition of Done:

- `ScreenStateCoordinator` wird nur noch innerhalb der Facade-nahen Implementierung beruehrt.
- Regression-Guard prueft, dass externe Module nicht direkt an `g_screenStateCoordinator` gelangen.
- Verhalten bleibt unveraendert.

## Zugpaket C3: Window-/Desktop-Operationen hinter Facade finalisieren

Ziel: Alle VM- und TVCALL-nahen Operationen, die Fenster/Desktop redrawen, laufen ueber einheitliche Base-Invalidation.

Umfang:

- Pruefen und ggf. umstellen:
  - `createEditWindow`
  - `deleteCurrentEditWindow`
  - `eraseCurrentEditWindow`
  - `modifyCurrentEditWindow`
  - `switchEditWindow`
  - `sizeCurrentEditWindow`
  - `zoomCurrentEditWindow`
  - `redrawCurrentEditWindow`
  - `redrawEntireScreen`
  - Virtual-Desktop Moves und Viewport Moves
- Direkte `drawView()`-Aufrufe in diesen Operationen bleiben erlaubt, muessen aber danach Base invalidieren oder Facade-Redraw melden.

Definition of Done:

- Jede dieser Operationen nutzt `returnWithDirectScreenMutation()` oder eine Facade-Methode.
- Macro-Overlay wird nach Base-Redraw konsistent reprojiziert.
- Guard prueft mindestens die wichtigsten Operationen textuell.

## Zugpaket C4: Marquee, Statusline, Frame, Indicator als Base-Sinks

Ziel: UI-Sinks, die ausserhalb der MacroCellGrid-Schicht zeichnen, melden Base-Mutation einheitlich.

Umfang:

- Pruefen:
  - `TMRMenuBar`
  - `TMRStatusLine`
  - `TMRFrame`
  - `TMRIndicator`
  - `TMREditorApp` globale Redraw-/Input-Pfade
- Bestehende `mrvmUiTouchScreenMutationEpoch()`-Aufrufe durch klarere Facade/API ersetzen, falls Paket C2 die API bereitstellt.
- Keine Umgehung von TVision. Es wird nur die Invalidation zentralisiert.

Definition of Done:

- Diese Sinks melden Base-Mutation ueber die Facade/API.
- Regression-Guard prueft die Aufrufe.
- Keine sichtbare Aenderung am UI-Verhalten.

## Zugpaket C5: MacroCellGrid Reprojection-Haertung

Ziel: Overlay-Projektion reagiert robust auf Base-Invalidation, Resize und KillBox.

Umfang:

- `MacroCellGrid::projectAll()` und `redrawBaseAndOverlay()` pruefen:
  - Base invalidiert
  - Terminalgroesse geaendert
  - Overlay vorhanden
  - Snapshot-Stack konsistent
- Wenn Base invalidiert ist: erst TVision-Base redrawen, dann bekannte MacroCell-Spans projizieren.
- Bei Resize: Grid und Snapshot-Stack invalidieren.

Definition of Done:

- Keine Ghosting-Reste bei `PUT_BOX`/`KILL_BOX` nach Window-Redraw.
- `CLEAR_SCREEN`, `SCROLL_BOX_*`, `CLR_LINE` bleiben stabil.
- Regression oder Probe deckt mindestens eine Base-Redraw-plus-Overlay-Sequenz ab.

## Zugpaket C6: TVCALL-Screen-Erweiterungen nur noch als Facade-Commands

Ziel: Neue TVCALL-Screen-Operationen duerfen nicht mehr direkt rendern.

Umfang:

- TVCALL Runtime Dispatch pruefen.
- Neue TVCALLs nur als:
  - `MRMacroDeferredUiCommand`
  - oder expliziter Facade-Call, wenn sie synchron UI-only sind.
- `VIDEO_MODE`, `TOGGLE`, `VIDEO_CARD` bleiben nicht implementiert.

Definition of Done:

- TVCALL-Surface-Guard bleibt gruen.
- Neue TVCALL-Screen-Ops koennen nicht am `MacroCellGrid` vorbei zeichnen.

## Zugpaket C7: Enforcement-Guards schaerfen

Ziel: Die Architekturgrenze wird maschinell verteidigt.

Umfang:

- Guard gegen `TScreen::screenBuffer` ausserhalb erlaubter Low-Level-/Teststellen.
- Guard gegen zweite `UiRenderFacade`.
- Guard gegen direkte Nutzung von `g_screenStateCoordinator` ausserhalb `mrvm.cpp`.
- Guard gegen neue Macro-Screen-Command-Renderer im Coprocessor.
- Guard gegen `flushScreen()` in VM-Screen-Ops ausserhalb Facade/Consumer.

Definition of Done:

- `make regression-check-core` enthaelt alle Guard-Faelle.
- Neue Architekturverletzungen schlagen lokal reproduzierbar fehl.

## Zugpaket C8: Performance-Coalescing

Ziel: Die Facade bleibt korrekt, aber vermeidet unnoetige Flushes.

Umfang:

- Dirty-Rect- oder Dirty-Row-Sammlung in `MacroCellGrid`.
- Batch-Flush pro Playback-Pump oder UI-Zyklus.
- `DELAY` und MessageBox bleiben Flush-Grenzen.
- Keine Aenderung der fachlichen Semantik.

Definition of Done:

- Viele kleine `WRITE`/`CLR_LINE`-Ops erzeugen weniger physische Flushes.
- Keine zusaetzlichen Ghosting-Effekte.
- Performance-Probe oder Logging kann Flush-Reduktion sichtbar machen.

## Empfohlene Reihenfolge

1. C1 Render-Sink-Klassifikation
2. C2 Facade-API fuer Base-Invalidation
3. C3 Window-/Desktop-Operationen
4. C4 Marquee/Statusline/Frame/Indicator
5. C5 MacroCellGrid Reprojection-Haertung
6. C6 TVCALL-Screen-Erweiterungen
7. C7 Enforcement-Guards
8. C8 Performance-Coalescing

Empfehlung: C1 bis C5 zuerst in kleinen Commits abschliessen. C8 erst danach, weil Coalescing sonst Korrektheitsfehler verdecken kann.
