# Option C: Direkte TV-Pfade hinter die Screen Facade ziehen

## Ist-Stand

Stand dieser Notiz: Codezustand nach den bisherigen C5-, C7- und C8-Arbeiten.

Bereits im Code verankert:

- VM/Mailbox-Screen-Ops laufen ueber `MRMacroDeferredUiCommand`.
- `mrvmUiRenderFacadeRenderDeferredCommand()` ist der zentrale Einstieg in die VM/UI Render Facade.
- `MacroCellGrid`/`MacroCellView` rendern Macro-Screen-Overlays.
- `ScreenStateCoordinator` trennt Base-Invalidation und Overlay-Mutation.
- Der Coprocessor besitzt nur noch ein `DeferredUiRenderGateway`, keine zweite `UiRenderFacade`.
- `returnWithDirectScreenMutation()` und `returnWithMacroScreenMutation()` markieren Base- bzw. Overlay-Mutationen zentral.
- Window-/Desktop-nahe VM-Pfade invalidieren die Base ueber die Facade.
- Kern-UI-Sinks wie MenuBar, StatusLine, Frame und Indicator melden Base-Invalidation.
- `MacroCellGrid` reprojiziert nach Base-Invalidation, Resize und `KILL_BOX` robuster.
- `CLEAR_SCREEN` verwirft veraltete `PUT_BOX`-Snapshots.
- Line-/Column-Overlay wird nach redraw-basierten `KILL_BOX`-Restorepfaden erneut aufgetragen.
- Der Coprocessor darf nicht direkt an neuen `mrvmUi*`-Screen-Renderern vorbei zeichnen.
- Deferred Playback batcht Macro-Screen-Projektionen pro Pump-Zyklus.
- Eine kleine Laufzeitprobe zeigt sichtbare Flush-Reduktion fuer batched gegen unbatched Playback.

Aktueller Gesamteindruck:

- C1 erledigt
- C2 erledigt
- C3 erledigt
- C4 erledigt
- C5 weitgehend erledigt
- C6 fuer die bisherige Surface erledigt
- C7 weitgehend erledigt
- C8 begonnen und funktional nachweisbar

## Zugpaket C1: Render-Sink-Klassifikation

Status: erledigt.

Im Code und in den Guards festgehalten:

- `ordinary-view-draw`
- `base-redraw-trigger`
- `overlay-render`
- `unsafe-physical-write`

Neue direkte physische Writes oder neue `flushScreen()`-Senken im VM-/Playback-Pfad fallen ueber `regression-check-core` auf.

## Zugpaket C2: Facade-API fuer Base-Invalidation

Status: erledigt.

Erreicht:

- `ScreenStateCoordinator` wird ausserhalb der Facade nicht direkt benutzt.
- Base-/Overlay-Mutationspfade laufen zentral ueber `UiScreenStateFacade`.
- `mrvmUiInvalidateScreenBase()` bleibt als oeffentliche API erhalten.
- Basis-Mutationspfade benutzen die Facade jetzt ueber `noteBaseMutation()`.
- die benannten Facade-Operationen `renderBaseThenOverlayIfNeeded()` und `renderOverlay()` existieren und werden im Macro-Screen-Pfad benutzt.

## Zugpaket C3: Window-/Desktop-Operationen hinter Facade finalisieren

Status: erledigt.

Erreicht:

- `createEditWindow`
- `deleteCurrentEditWindow`
- `eraseCurrentEditWindow`
- `modifyCurrentEditWindow`
- `switchEditWindow`
- `sizeCurrentEditWindow`
- `zoomCurrentEditWindow`
- `redrawCurrentEditWindow`
- `redrawEntireScreen`

Diese Pfade invalidieren die Base ueber `returnWithDirectScreenMutation(...)`.

## Zugpaket C4: Marquee, Statusline, Frame, Indicator als Base-Sinks

Status: erledigt.

Erreicht:

- MenuBar, StatusLine, Frame, Indicator und relevante App-Pfade invalidieren die Base zentral.
- Keine sichtbare Verhaltensaenderung war dafuer notwendig.

## Zugpaket C5: MacroCellGrid Reprojection-Haertung

Status: weitgehend erledigt.

Erreicht:

- `projectAll()` und `redrawBaseAndOverlay()` behandeln Base-Invalidation konsistent.
- Resize setzt einen expliziten Reprojection-Pending-Zustand.
- `KILL_BOX` redrawt nach Resize/Restorepfaden sauber nach.
- `CLEAR_SCREEN` loescht aktive Box-Snapshots, damit spaetere Restorepfade kein veraltetes Bild wiederherstellen.
- `PUT_LINE_NUM`/`PUT_COL_NUM` bleiben nach redraw-basierten Restorepfaden sichtbar.

Bereits abgesicherte Problemklassen:

- `PUT_BOX`/`KILL_BOX` nach Base-Redraw
- Resize plus `KILL_BOX`
- `CLEAR_SCREEN` plus Snapshot-Stack
- Line-/Column-Overlay nach `KILL_BOX`

Restoffen:

- keine klar dringende fachliche Kante mehr bekannt
- echte Sichtprobe im laufenden UI bleibt trotzdem sinnvoller als weiterer Guard-Ausbau

## Zugpaket C6: TVCALL-Screen-Erweiterungen nur noch als Facade-Commands

Status: fuer die aktuelle Surface erledigt.

Erreicht:

- TVCALL-Runtime bleibt auf der bestehenden Surface begrenzt.
- `VIDEO_MODE`, `VIDEO_CARD` und `TOGGLE` bleiben explizit nicht implementiert.
- Neue TVCALL-Screen-Ops koennen nicht still am `MacroCellGrid` vorbei eingefuehrt werden.
- Visualisierende TVCALLs laufen nun auch im Vordergrund ueber den zentralen Deferred-Command-Pfad statt ueber einen separaten Direktpfad.
- Die visuellen Makro-Screen-Prozeduren (`MARQUEE`, `WORKING`, `BRAIN`, `PUT_BOX`, `WRITE`, `CLR_LINE`, `GOTOXY`, `PUT_LINE_NUM`, `PUT_COL_NUM`, `SCROLL_BOX_*`, `CLEAR_SCREEN`, `KILL_BOX`) benutzen denselben Deferred-Command-Kanal; im Hintergrund gestaged, im Vordergrund ueber dieselbe Facade sofort konsumiert.

## Zugpaket C7: Enforcement-Guards schaerfen

Status: weitgehend erledigt.

Erreicht:

- Guard gegen `TScreen::screenBuffer` im VM-/Playback-Pfad
- Guard gegen zweite `UiRenderFacade`
- Guard gegen direkte Nutzung von `g_screenStateCoordinator` ausserhalb der Facade
- Guard gegen direkte physische Render-Sinks im Coprocessor-Playback
- Guard gegen direkte `mrvmUi*`-Screen-Renderer im Coprocessor ausserhalb des zentralen Gateway-Pfads
- Whitelist der erlaubten `mrvmUi*`-Bridge-Aufrufe im Coprocessor
- Guard gegen `flushScreen()` ausserhalb der freigegebenen VM-Senken

Freigegebene `flushScreen()`-Senken im VM-Pfad:

- `forceMacroUiMessageRefresh()`
- `MacroCellGrid::endProjectionBatch()`
- `MacroCellGrid::projectAll()`
- `MacroCellGrid::redrawBaseAndOverlay()`

## Zugpaket C8: Performance-Coalescing

Status: weitergezogen und funktional nachweisbar.

Erreicht:

- Dirty-Row-Sammlung in `MacroCellGrid`
- Batch-Flush pro Deferred-Playback-Pump
- physischer Flush erst am Batch-Ende statt bei jeder kleinen Overlay-Mutation
- `BRAIN` redrawt nur noch den aktiven sichtbaren Fensterrahmen statt pauschal alle Edit-Frames.
- Recording-/Brain-Marker-Blink redrawt nur noch den aktiven sichtbaren Fensterrahmen.
- Vordergrund-Rendering fuer `MARQUEE*` und `BRAIN` vermeidet jetzt No-op-Refreshes und ist damit naeher an der geaendert/nicht-geaendert-Semantik des Deferred-Playbacks.

Sichtbare Probe:

- Probe-Modus im bestehenden Regressionsbinary vorhanden
- Lauf mit TTY zeigte:
  - `unbatched=8`
  - `batched=1`
  - `reduction=7`

Restoffen:

- weitere fachliche Feinoptimierung nur dann, wenn sie ohne neuen Guard- oder Testaufbau klaren Nutzen bringt

## Wrapper-Befund

Status: kein repo-weites Grundproblem, aber ein lokaler Hotspot in `mrvm.cpp`.

Einordnung:

- Viele kleine Hilfen in Dialogen und UI-Klassen sind legitime Adapter auf TVision- oder Settings-Domaenen und kein AGENTS-Verstoss.
- Auffaellige Redundanz gab es lokal in `mrvm.cpp`, vor allem bei ASCII-Uppercase-Helfern.
- Diese Redundanz ist reduziert; offensichtliche Standard-Library-Vergleiche wurden ebenfalls vereinfacht.
- Die manuellen Pfad-/Extension-Helfer in `mrvm.cpp` bleiben vorerst stehen, weil sie bewusst gemischte `/`- und `\\`-Semantik sowie DOS-artige Laufwerksfaelle behandeln und nicht als triviale Standard-Library-Ersetzung zu werten sind.

## Empfehlung ab hier

Der Strangler-Zug ist aus meiner Sicht an einem vernuenftigen Zwischenabschluss.

Empfehlung:

1. Keine weitere Breitenarbeit an Guards oder Regressionen.
2. Wenn noch etwas folgen soll, dann nur eine kurze manuelle Sichtprobe mit `mrmac/macros/test_ch23_screen_ops.mrmac`.
3. Danach diesen Abschnitt als vorlaeufig abgeschlossen betrachten.

Nicht empfohlen:

- weitere Guard-Taxonomie ohne neuen fachlichen Befund
- weiterer Probe-/Regressionstest-Ausbau nur um der Vollstaendigkeit willen
