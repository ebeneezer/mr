# Macro Debugger Integrationsplan

## Ziel

MR bekommt einen source-genauen Debugger fuer Macros aus der Macro-Registry:

- Registry-Macro zum Debuggen starten.
- Source-genaue Breakpoints setzen und treffen.
- Step Into, Step Over, Step Out, Continue, Pause und Stop.
- Variablen zunaechst read-only inspizieren.
- Locals, file globals und weitere Runtime-Bereiche im Variables-Pane zeigen.
- Bestehende BentoBox-Panes fuer Source, Debugger Output, Variables und Watches
  nutzen.
- Execution Sessions beobachtbar machen, ohne die base Session Runtime zu einem
  Debugger-Subsystem auszubauen.

Nicht-Ziel: Bytecode-Ansicht als Benutzerfunktion.

## Leitpraemisse

Der Debugger implementiert keine Parallelstrukturen. Er ist Consumer
vorhandener Runtime-Grundlagen:

- `MRMacroExecutionSession` und session status/result publication,
- `MRMacroExecutionProfile` und current route classification,
- `Lane::Macro` fuer background macro work,
- staged input/result/commit flow,
- deferred UI playback,
- zentrale VM K/V Runtime Roots fuer macro-sichtbaren Zustand.

Nicht einfuehren:

- debugger-only macro execution lane,
- debugger-only macro runner,
- Bytecode-Injection-API am canonical compiler path vorbei,
- debugger-private copy of staged editor state,
- debugger-private global/runtime K/V store,
- direct TVision rendering from debugger or execution-session code,
- alternate deferred UI batching boundaries,
- session-level shortcuts, die staged conflict checks umgehen,
- debugger-only state im base session model.

## Protected Architecture

Protected architecture touched: yes, sobald Code implementiert wird.

Betroffene Vertraege:

- `documentation/architecture/mrmac-language-contract.md`
- `documentation/architecture/mrmac-exec-session-contract.md`
- `documentation/architecture/vm-deferred-ui-contract.md`
- `documentation/architecture/coprocessor-deferred-ui-contract.md`
- `documentation/architecture/tvision-integration-contract.md`
- `documentation/architecture/build-regression-contract.md`

Betroffene geschuetzte Bereiche fuer spaetere Code-Slices:

- `mrmac/mrmac.c`, `mrmac/mrmac.h`: Source-Map im canonical bytecode path.
- `mrmac/MRVM.cpp`, `mrmac/MRVM.hpp`: debugfaehige Ausfuehrung, Breaks,
  Stack-/Variablen-Snapshots.
- `mrmac/MRMacroRunner.cpp`, `mrmac/MRMacroRunner.hpp`: Debug-Start aus
  Registry-Macros.
- `coprocessor/MRCoprocessor*`, `coprocessor/MRCoprocessorDispatch*`: falls
  Debug-Ausfuehrung workergefuehrt oder gepumpt wird.
- `ui/MRBentoBox*`: Debugger Output, Watches, Variables, Source-Marker.

Invarianten:

- Kein zweiter Compiler-Frontend.
- Keine Opcode-Umnummerierung.
- Keine neue Macro-Lane ohne eigene Architekturentscheidung.
- Keine direkten TVision-Screenbuffer-Writes.
- Kein Render-Seitenkanal fuer Debugger-Overlay.
- Typed UI procedures und Macro-Screen-Operationen bleiben
  staged/projection-basiert, soweit sie nicht bereits UI-thread-only laufen.
- Breakpoints bleiben im ersten Entwurf ephemeral pro Debug-Session und werden
  nicht in Settings oder Workspace persistiert.

## Bestehende Ausfuehrungsrouten

Der Macro Runner kennt bereits drei Routen:

- UI-thread execution fuer Macros, die live UI state direkt beruehren muessen.
- Background-safe execution auf `Lane::Macro`.
- Staged background execution auf `Lane::Macro` fuer unterstuetzte UI-affine
  Editor- und Runtime-Operationen.

`MRMacroExecutionProfile`, `mrvmAnalyzeBytecode()`,
`mrvmCanRunInBackground()` und `mrvmCanRunStagedInBackground()` entscheiden, ob
ein Macro den UI-thread path verlassen darf. Exec Sessions duerfen diese Routen
benennen und Status veroeffentlichen, aber ihre Semantiken nicht zu einem
generischen Pfad verschmelzen.

## Staged Background Flow

Die staged background route wird in `MRMacroRunner.cpp` gewaehlt, wenn:

- kein spezifischer selected unit name ausgefuehrt wird,
- `mrvmCanRunStagedInBackground(profile)` das Bytecode-Profil akzeptiert,
- ein aktives Editorfenster und ein Editor verfuegbar sind.

Der Runner baut `MRMacroStagedExecutionInput` aus document snapshot, base
version, cursor, selection, block state, buffer/window identity, geometry,
macro-visible globals, loaded macro names, last search state, runtime options,
mark stack, insert mode, indent level, file name, dirty state, screen size und
screen cursor position. Zusaetzlich entsteht ein `MacroCommitConflictSnapshot`.

Der Work Item laeuft als `TaskKind::MacroJob` auf `Lane::Macro`.
`mrvmRunBytecodeStagedBackground()` installiert eine `BackgroundEditSession`.
Editor-Mutationen werden als `StagedEditTransaction`, geeignete Screen-/UI-
Operationen als deferred UI commands gesammelt.

`MRCoprocessorDispatch.cpp` empfaengt `MacroJobStagedPayload` und committed nur
bei passendem Live-State:

- target editor per buffer id finden,
- document version und runtime conflict snapshot vergleichen,
- staged edit transaction committen,
- staged Runtime State anwenden,
- deferred UI playback erst nach erfolgreichem Commit queueen,
- execution-session terminal result als completed oder rejected publizieren.

Es gibt keinen Rebase-Pfad. Ein Konflikt bricht den Commit ab.

Vorhandene staged Regression-Probes:

- `mrmac/macros/test_search_replace_staged.mrmac`
- `mrmac/macros/test_window_ui_commands_staged.mrmac`
- `mrmac/macros/test_global_state_staged.mrmac`
- `mrmac/macros/test_runtime_options_staged.mrmac`
- `mrmac/macros/test_mark_stack_staged.mrmac`
- `mrmac/macros/test_block_state_staged.mrmac`
- `mrmac/macros/test_run_macro_session_state_staged.mrmac`

## Exec Session Und Debugger

Der Macro Debugger ist Consumer von MRMac Execution Sessions. Start, Pause,
Continue, Stop, Cancellation, Yield/Resume und Result-Routing laufen ueber den
Session-Kern.

Die base Execution Session bleibt Substrat fuer route, owner, lifetime, task id,
cancellation, yield/resume state und result publication. Debugger-spezifisch
bleiben Source-Map, Breakpoints, Step-Semantik, Source-Location-Snapshots,
Variablen-/Call-Frame-Praesentation, Debugger UI state und Audit-/Timeline-
Praesentation.

Debugger controls mappen auf Session Control:

- start creates an Exec Session,
- pause stops at cooperative VM boundaries,
- step resumes one debuggable unit,
- continue resumes until the next stop condition,
- cancel requests session cancellation,
- inspect reads the session-owned VM/debug snapshot.

Nested `RUN_MACRO(...)` calls erscheinen als Debugger Frames. Wenn ein
aufgerufenes Macro keine Source-Map hat, zeigt der Debugger einen external oder
opaque frame statt Source-Positionen zu erfinden.

## Session Observability

Der Macro Debugger soll die native Beobachtungsoberflaeche fuer Exec Sessions
werden. Die base Runtime liefert Rohdaten, aber keine zweite Debugger-UI oder
Ergonomie-Schicht.

Required information:

- session id,
- owner kind und owner id,
- macro spec,
- source package, soweit verfuegbar,
- start time, end time, current state, result status,
- cancellation reason,
- skip reason fuer scheduled runs,
- parent session oder caller frame, soweit verfuegbar,
- last VM error text, soweit verfuegbar.

Die Debugger-UI soll Ursacheketten erklaeren koennen: menu start, scheduled
tick, modeless UI callback, skipped tick wegen aktiver Session, owner shutdown,
modeless window close.

Audit Mode braucht kein source-level Debugging, sondern eine Session Timeline
und genug Ownership-Daten, um run/skip/stop/fail zu erklaeren.

## Debug Semantik

Der erste Macro Debugger fuehrt echte Macro-Semantik aus und pausiert
kooperativ an VM-Instruktionsgrenzen. Eine Debug Session startet eine Exec
Session; Step/Continue/Pause/Stop sind Control Requests an dieselbe Session.
Es gibt keine neue Session pro OP.

Die VM laeuft bis zu einer Stop-Policy:

- breakpoint offset reached,
- step condition satisfied,
- pause requested,
- cancel requested,
- VM error,
- HALT / macro complete,
- execution budget exhausted.

Side effects bleiben im ersten Debugger-Slice real. Ein staged preview debugger
ist nicht Teil des ersten Plans, weil Breakpoints, Variablen, Editorzustand und
deferred UI dann vor finalem Commit beobachtet werden muessten.

## UX

Die BentoBox bleibt frei platzierbar:

- Source Pane: vorhandener Source-Editor mit Breakpoint-Markern und aktueller
  Ausfuehrungszeile.
- Debugger Output: Debug-Status, Treffer, Fehler, Stop/Pause-Meldungen.
- Variables: aufklappbare Gruppen.
- Watches: spaeter explizite Ausdruecke, zunaechst optional leer.

Controls werden als Commands/Toolbar-/Menu-/Keymap-Aktionen geplant:

- Start Debug Macro
- Reload Debug Macro
- Continue
- Pause
- Stop
- Step Into
- Step Over
- Step Out
- Toggle Breakpoint

Controls rendern nicht direkt. Sie aendern Debugger-Zustand; BentoBox und
Editor-Projektion zeichnen TVision-konform.

## Debug Target

Das Debug-Ziel ist ein Macro aus der Registry:

- Der Debugger arbeitet auf dem vom Runtime-System bekannten Macro-Begriff.
- Dateien mit mehreren Macros muessen nicht als Primaer-UX geloest werden.
- `RUN_MACRO(...)` und gebundene Registry-Macros koennen spaeter konsistent in
  Step Into einbezogen werden.

Erforderlich:

- Registry-Eintrag muss auf Ursprungspfad und Macro-Name abbildbar sein.
- Source-Map muss pro kompilierter Macro-Datei mehrere Macros unterscheiden.
- Debugger startet auf dem gewaehlten Macro-Entry.

## Source Map

Der Compiler muss beim canonical bytecode generation path eine Source-Map
erzeugen:

- bytecode offset / instruction start,
- source file path oder source identity,
- source line,
- source column oder source offset, soweit stabil verfuegbar,
- macro name / macro entry range.

Die Source-Map gehoert zur letzten Compile-Operation oder zu einem expliziten
Compile-Ergebnis fuer Debugging. Sie darf kein zweiter Compilerpfad werden.

Breakpoints werden source-seitig, session-lokal, pro Macro-Source-Identity und
line-basiert gesetzt. Der Debugger normalisiert auf die naechste debuggable
instruction. Nicht debuggable Zeilen muessen sichtbar als disabled/unbound oder
als gebundene naechste gueltige Zeile behandelt werden. Keine Bytecode-Heuristik
in der UI.

## Debug Session Model

Es braucht eine interne Debug-Session, aber keine Benutzer-Shell. Die
Debug-Session referenziert eine MRMac Execution Session und konsumiert deren
Laufzeitstatus. Sie besitzt Breakpoints, Step-Modus und Debugger-Snapshots, aber
nicht die VM-Ausfuehrungsroute selbst.

Die Debug-Session besitzt:

- ausgewaehltes Registry-Macro,
- kompilierte Bytecode-Kopie,
- Source-Map,
- aktuelle VM-/Session-Referenz,
- Status: idle, running, paused, stopped, completed, failed,
- Breakpoints und Step-Modus,
- current source location,
- variable snapshot,
- call stack / macro invocation stack,
- letzte Debugger-Meldung.

Die UI liest Snapshots. Sie besitzt nicht die VM.

## VM Debugging

Die VM braucht einen debugfaehigen Ausfuehrungspfad:

- Ausfuehrung bis Breakpoint, Step-Bedingung, Pause-Anforderung,
  Stop-Anforderung, Fehler oder Halt.
- Kooperative Pause/Stop an VM-Instruktionsgrenzen.
- Kein Abbruch mitten in einem C++ intrinsic, externem I/O oder UI playback.
- `DELAY` bleibt kooperativ pausierbar/stopbar, soweit der vorhandene
  Delay-Mechanismus das erlaubt.

Step-Semantik:

- Step Into betritt `CALL` und `RUN_MACRO(...)`.
- Step Over fuehrt Calls bis zur naechsten source line im aktuellen Frame aus.
- Step Out laeuft bis zur Rueckkehr aus dem aktuellen Frame.
- Continue laeuft bis Breakpoint, Pause, Stop, Fehler oder Halt.

## Variables Pane

Erster Umfang: read-only Inspector.

Gruppen:

- Locals
- File globals
- App globals
- Weitere Runtime-Globals, falls im VM-Modell vorhanden
- Stack optional nur fuer Diagnose, nicht als Standard-UX

Darstellung:

- Aufklappbare Gruppen.
- Eingerueckte Variablen.
- Hashes und Arrays rekursiv aufklappbar, aber budgetiert.
- Detailansicht bei Auswahl einer Variable.

Mutation bleibt spaeterer Slice und muss typisiert, validiert und an
VM-Speichergrenzen gebunden sein. Hashes, Arrays und globale Runtime-Stores
duerfen nicht ueber UI-Schattenzustand mutiert werden.

## Threading And Pumping

Der Debugger darf den UI-Thread nicht durch lange Macro-Ausfuehrung blockieren.

Zulaessige Richtung:

- VM laeuft budgetiert oder workergefuehrt.
- Debug-Events und Snapshots gehen kontrolliert an den UI-Thread.
- UI zeichnet TVision-konform ueber vorhandene Views/Panes.

Nicht zulaessig:

- direktes Rendering an TVision vorbei,
- eigenes Overlay mit Screenbuffer-Zugriff,
- versteckte UI-Side-Effects aus generischen VM-Intrinsics,
- neue deferred playback boundaries als Nebeneffekt.

Eine pausierte Debug-Session darf keinen Worker blockieren. Bei Breakpoint oder
Step-Ende wird die VM als Session-Zustand geparkt, der Worker gibt frei, und
Continue/Step queued wieder Arbeit ueber die bestehende Macro-Ausfuehrung.

## Implementation Slices

1. Architecture Decision And Source Map Plan
   - Dateien: `documentation/supergeiler-macro-debugger.md`, ggf. spaetere
     Architektur-Entscheidungsnotiz.
   - Ziel: Source-Map und Debug-Ausfuehrung festlegen, keine Codeaenderung.

2. Compiler Source Map
   - Dateien: `mrmac/mrmac.c`, `mrmac/mrmac.h`, ggf. `mrmac/MRMacroRunner.*`.
   - Ziel: Source-Map im canonical compiler path erzeugen, Macro entries
     source-genau zuordnen, keine Grammar-/Opcode-Semantik aendern.

3. VM Debug Step Core
   - Dateien: `mrmac/MRVM.hpp`, `mrmac/MRVM.cpp`.
   - Ziel: VM bis zu Debug-Stoppunkten laufen lassen, Snapshots fuer IP, call
     stack, locals und globals erzeugen, normale Macro-Ausfuehrung unveraendert
     lassen.

4. Registry Debug Start
   - Dateien: `mrmac/MRMacroRunner.*`, ggf. App command/router/menu-Dateien.
   - Ziel: Registry-Macro als Debug-Ziel auswaehlen, Debug-Ausfuehrung starten,
     Fehler sichtbar melden.

5. Bento Debug UI
   - Dateien: `ui/MRBentoBox.hpp`, `ui/MRBentoBoxProjection.cpp`,
     `ui/MRBentoBoxDiagnostics.cpp` oder passender bestehender Bento-Teil, ggf.
     `ui/MRFileEditor/*`.
   - Ziel: bestehende Rollen `Debugger Output`, `Variables`, `Watches`
     befuellen, Source-Ausfuehrungszeile und Breakpoints anzeigen.

6. Watches And Mutation
   - Dateien: erst nach Review von Slice 2-5 festlegen.
   - Ziel: Watch-Ausdruecke, spaeter typisierte Variable-Mutation.

## Regression And Manual Checks

Pflicht bei relevanten Code-Slices:

- `make clean all CXX=clang++`
- representative Macro compile
- VM execution of compiled bytecode
- Source-Map: mehrere Macros in einer Datei, Breakpoint-Zeilen mit und ohne
  emitted instruction
- Debug run: Continue, Pause, Stop
- Step Into, Step Over, Step Out
- Step Into ueber `RUN_MACRO(...)`
- Locals/file globals/app globals im Variables-Pane
- Hash/Array read-only Darstellung
- foreground UI-thread macro execution
- foreground `DELAY` yield, resume and cancel, falls beruehrt
- background-safe `Lane::Macro` execution, falls beruehrt
- staged macro execution, conflict rejection and successful commit, falls
  beruehrt
- deferred UI playback ordering and batching, falls beruehrt
- UI: Bento Pane placement, focus traversal, resize, redraw, modal open/close
- Coprocessor/deferred UI checks, falls worker route oder playback beruehrt wird

## Open Decisions

- Separate Architekturvertragsdatei fuer Macro Debugging vor VM-/Compiler-Code?
- Debug-Ausfuehrung budgetiert im UI-Pump oder workergefuehrt auf der
  bestehenden Macro-Lane?
- Wie werden file globals im vorhandenen Runtime-Modell von app globals
  abgegrenzt?
- Wie wird ein Registry-Macro eindeutig auf Source-Pfad und Source-Map-Version
  gebunden?
- Welche Intrinsics gelten beim Debuggen als nicht step-in-faehige black boxes?
- Wie gross darf der Snapshot fuer Hashes/Arrays im Variables-Pane sein?
- Welche minimale Session-Snapshot-Hook-Schnittstelle braucht die VM, ohne
  debugger-only state in das base session model zu backen?

## Contract References

- `documentation/architecture/README.md`
- `documentation/architecture/mrmac-language-contract.md`
- `documentation/architecture/mrmac-exec-session-contract.md`
- `documentation/architecture/vm-deferred-ui-contract.md`
- `documentation/architecture/coprocessor-deferred-ui-contract.md`
- `documentation/architecture/tvision-integration-contract.md`
- `documentation/architecture/build-regression-contract.md`

Die foundation reuse invariant aus `documentation/architecture/README.md` gilt
direkt: Der Debugger erweitert vorhandene execution/session/staging foundations
statt sie in parallelen Spezialpfaden nachzubauen.
