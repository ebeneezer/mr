# Macro Debugger Integrationsplan

## Ziel

MR bekommt einen source-genauen Debugger fuer Macros aus der Macro-Registry:

- Registry-Macro zum Debuggen starten.
- Source-genaue Breakpoints setzen und treffen.
- Step Into, Step Over, Step Out, Continue, Pause und Stop.
- Alle tatsaechlichen Variablenscopes inspizieren und pausiert mutieren.
- Locals, Closure-, Session- und App-Globals im Variables-Pane zeigen; ein
  File-Global-Scope existiert in MRMAC nicht.
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
- Source-Maps unter `MACROCATALOG`, Debugger-Daten unter `MACRODEBUGGER`.

Nicht einfuehren:

- debugger-only macro execution lane,
- debugger-only macro runner,
- Bytecode-Injection-API am canonical compiler path vorbei,
- debugger-private copy of staged editor state,
- debugger-private global/runtime K/V store,
- debugger-private Source-Map registry,
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
- Live gebundene Breakpoints und Watches liegen im zentralen VM K/V Store unter
  `MACRODEBUGGER`. Der Workspace darf die kalte Definition eines
  Debugger-Bentos persistieren; Session, VM, Source-Map, Bytecode-Bindung und
  Werte bleiben runtime-only.

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

Side effects bleiben real. Der Debugger waehlt denselben natuerlichen
Ausfuehrungsweg wie der normale Runner: background-safe Bytecode laeuft in
endlichen Macro-Worker-Zuegen, staged-faehiger Bytecode auf dem vorhandenen
Snapshot/Conflict/Commit-Pfad, und verbleibender UI-affiner Bytecode
budgetiert im UI-Pump. Es gibt keinen separaten Preview-Modus.

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
- Toggle Breakpoint. Im ersten UI-Slice toggelt `F9` den Line-Breakpoint auf
  der Quellzeile des Source-Cursors und meldet set/cleared/unbound im
  Debugger-Output.

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

Der normale Macro-Load erzeugt keine Source-Map. Wenn aus der Macro Library der
Debug-Button fuer das fokussierte Macro ausgeloest wird, kompiliert der
Debug-Start das geladene Macro-File mit Source-Map:

- bytecode offset / instruction start,
- source file path oder source identity,
- source start offset,
- source end offset,
- source line,
- source column, soweit der Lexer sie sauber mitfuehren kann,
- macro name / macro entry range.
- debuggable kind: macroEntry, statement, expression, call, branch, label.

Die Source-Map gehoert nicht in eine zweite C++-Registry. Der Compiler darf
mechanische Transferdaten fuer das Debug-Compile-Ergebnis liefern; die
runtime-sichtbare Ablage erfolgt im zentralen VM K/V Store unter
`MACROCATALOG` fuer geladene Macro-Dateien. Rein debugger-kontrollierte und
generierte Daten liegen unter `MACRODEBUGGER`. Die Source-Map darf kein zweiter
Compilerpfad werden.

Die interne Source-Map ist token-/span-genau. Zeilen sind nur eine
UI-kompatible Sicht auf diese Spans. Ein line breakpoint bindet im ersten
UI-Slice an den ersten debuggable span der Zeile; spaetere UI kann mehrere
debuggable spans in derselben Zeile unterscheiden.

Breakpoints werden source-seitig und pro Macro-Key unter `MACRODEBUGGER`
gesetzt. Der Debugger normalisiert auf debuggable spans. Nicht debuggable
Positionen muessen sichtbar als disabled/unbound behandelt werden. Keine
Bytecode-Heuristik in der UI.

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

Der Inspector zeigt und mutiert die tatsaechliche pausierte VM.

Gruppen:

- Locals
- App globals
- Closure variables
- Execution-session variables
- Stack optional nur fuer Diagnose, nicht als Standard-UX

Darstellung:

- Aufklappbare Gruppen.
- Eingerueckte Variablen.
- Hashes und Arrays rekursiv und vollstaendig hierarchisch dargestellt.
- Zyklen werden als Referenzen markiert; es gibt keine willkuerliche
  Snapshot-Groessengrenze.
- Detailansicht bei Auswahl einer Variable.

Mutation ist typisiert, validiert und an die VM-Speichergrenzen gebunden:
skalare Werte werden typgleich ersetzt, Hash-Eintraege koennen hinzugefuegt,
umbenannt oder entfernt und Array-Elemente angehaengt oder entfernt werden.
Jeder Write validiert Root, Scope, Typ und kompletten Collection-Pfad neu.
Hashes, Arrays und Runtime-Stores werden nie ueber UI-Schattenzustand mutiert.

## Threading And Pumping

Der Debugger darf den UI-Thread nicht durch lange Macro-Ausfuehrung blockieren.

Festgelegte Richtung:

- UI-affine VM-Ausfuehrung laeuft budgetiert im UI-Pump.
- Background-safe und staged Ausfuehrung laeuft in endlichen Worker-Zuegen auf
  der bestehenden Macro-Lane.
- Debug-Events und Snapshots gehen kontrolliert an den UI-Thread.
- UI zeichnet TVision-konform ueber vorhandene Views/Panes.

Nicht zulaessig:

- direktes Rendering an TVision vorbei,
- eigenes Overlay mit Screenbuffer-Zugriff,
- versteckte UI-Side-Effects aus generischen VM-Intrinsics,
- neue deferred playback boundaries als Nebeneffekt.

Eine pausierte Debug-Session blockiert keinen Worker. Bei Breakpoint oder
Step-Ende wird die VM als Session-Zustand geparkt, der Worker gibt frei, und
Continue/Step queued wieder Arbeit ueber die bestehende Macro-Ausfuehrung.
Pause und Stop setzen kooperative Kontrollflags, ohne auf den VM-Lock des
laufenden Workers zu warten. Intrinsics, externes I/O und deferred playback
bleiben atomare Blackboxes fuer Step/Pause.

## Implementation Slices

1. Architecture Decision And Source Map Plan
   - Dateien: `documentation/supergeiler-macro-debugger.md`, ggf. spaetere
     Architektur-Entscheidungsnotiz.
   - Ziel: Source-Map und Debug-Ausfuehrung festlegen, keine Codeaenderung.
   - Ergebnis: reviewbarer Vertrag fuer Slice 2/3, nicht schon
     Implementierung.

   Zugplanung:

   - Source-Map-Form festlegen:
     - Owner bleibt der canonical compiler path in `mrmac/mrmac.c`.
     - Inhalt: bytecode offset, source identity, start/end source offset, line,
       optional column, macro name, debuggable kind.
     - Interne Bindung ist token-/span-genau; line breakpoints sind nur ein
       UI-kompatibler Normalisierungsmodus.
     - Keine Opcode-Aenderung, kein zweiter Compilerpfad, keine
       UI-Bytecode-Heuristik.
   - Debug-Session-Grenze festlegen:
     - Debug-Session besitzt Breakpoints, Step-Mode, Source-Map und
       UI-Snapshots.
     - Exec Session besitzt route, owner, lifetime, cancel/yield/result.
     - VM besitzt Ausfuehrungszustand und Stop-Policy.
   - Stop-Policy festlegen:
     - Continue bis Breakpoint, Pause, Cancel, Error, Halt oder Budget.
     - Step Into/Over/Out an source-mapped instruction boundaries.
     - Kein Abbruch mitten in Intrinsic, I/O oder deferred playback.
   - Registry-Binding festlegen:
     - Debug-Ziel bleibt Registry-Macro.
     - Macro muss auf Source-Identity und Macro-Entry abbildbar sein.
     - Mehrere Macros pro Datei muessen unterscheidbar sein.
   - Natuerliche Route festlegen:
     - Debugger klassifiziert Bytecode wie der normale Runner.
     - Staged Debugging verwendet denselben Input-, Konflikt-, Commit- und
       Deferred-Playback-Pfad; es gibt keinen Preview-Sonderweg.
   - Abnahmekriterien:
     - Source-Map-Struktur ist fuer Slice 2 konkret genug.
     - Compile-Ergebnis und span-basierte Breakpoint-Normalisierung sind
       beschrieben.
     - Session/Debugger/VM-Verantwortung ist trennscharf.
     - Keine Codeaenderung und kein Build fuer Slice 1.

2. Compiler Source Map
   - Dateien: `mrmac/mrmac.c`, `mrmac/mrmac.h`, ggf. `mrmac/MRMacroRunner.*`.
   - Ziel: Source-Map im canonical compiler path erzeugen, Macro entries
     source-genau zuordnen, keine Grammar-/Opcode-Semantik aendern.
   - Ergebnis: der canonical compiler path liefert Source-Map-Transferdaten;
     RuntimeCatalog speichert die Source-Map im zentralen VM K/V Store.

   Zugplanung:

   - Datenmodell fuer Source-Map-Eintraege festlegen:
     - `bytecodeOffset`
     - `sourceStartOffset`
     - `sourceEndOffset`
     - `line`
     - `column` als optionaler Wert, `0` falls nicht gepflegt
     - `macroName`
     - `debuggableKind`
   - Compiler-Transfer statt Registry:
     - `mrmac.c` darf Source-Map-Eintraege nur waehrend der aktuellen
       Compilation als mechanische Transferdaten bereitstellen.
     - Keine dauerhaft werttragende Source-Map-Struktur neben VM K/V.
     - Keine Speicherung im Bytecode.
  - Runtime-K/V-Ablage:
    - Nur Debug-Starts erzeugen Source-Maps. Normaler Macro-Load und
      Bytecode-Refresh bleiben SourceMap-frei.
    - Geladene Macro-Dateien speichern debugger-erzeugte Source-Maps unter
      `MACROCATALOG/files/byKey/<fileKey>/sourceMap`.
     - Debugger-generierte Laufzeitdaten liegen unter
       `MACRODEBUGGER/sessions/byId/<debugSessionId>`.
     - Breakpoints liegen unter
       `MACRODEBUGGER/breakpoints/byMacro/<macroKey>`.
     - Debugger-Snapshots liegen unter
       `MACRODEBUGGER/sessions/byId/<debugSessionId>/snapshots`.
     - C++-Objekte sind nur Transfer-/Snapshot-Objekte und werden aus K/V
       rebuilt.
   - Lexer/Parser-Minimum:
     - Token bekommt `sourceStartOffset`.
     - Parser-Grenzen berechnen `sourceEndOffset` aus konsumierten Tokens.
     - `line` bleibt vorhanden.
     - `column` wird nur gesetzt, wenn ohne breiten Lexer-Umbau sauber
       pflegbar; sonst bleibt sie `0`.
   - Emission-Mapping:
     - Map-Eintraege werden an debuggable emission boundaries geschrieben.
     - Primaere Boundaries: macro entry, statement start, call/proc/intrinsic,
       branch/goto/call-label operations.
     - Labels selbst sind source spans, aber nicht zwingend stoppbare
       Ausfuehrungspunkte.
   - Macro-Zuordnung:
     - Jeder Source-Map-Eintrag traegt den aktuellen Macro-/Closure-Namen.
     - Macro entry ranges bleiben ueber bestehende compiled macro metadata
       rekonstruierbar.
  - RuntimeCatalog-Integration:
    - Source-Map wird erst beim Debug-Start in `LoadedMacroFile` geschrieben
      und gelesen.
     - `LoadedMacroFile` darf eine Transferdarstellung tragen, bleibt aber
       nicht authoritative; authoritative ist `MACROCATALOG`.
   - Fehler-/Grenzfaelle:
     - Bei Compile-Fehlern ist die Source-Map leer oder unvollstaendig und
       nicht fuer Debugger-Binding zu verwenden.
     - Nicht debuggable spans werden nicht geraten; die UI normalisiert spaeter
       auf vorhandene debuggable spans.
   - Abnahmekriterien:
     - Mehrere Macros in einer Datei liefern unterscheidbare spans.
     - Mehrere debuggable Tokens in einer Zeile bleiben unterscheidbar.
     - Line breakpoint kann auf ersten debuggable span einer Zeile normalisiert
       werden.
     - Bytecode bleibt bitweise semantisch unveraendert.
     - Existing macro compile/run bleibt unveraendert.
     - Keine neue Source-Map-Registry ausserhalb von `MACROCATALOG`.
     - Keine debugger-kontrollierten Daten ausserhalb von `MACRODEBUGGER`.

   Slice 2b:

   - `MRVMRuntimeCatalog` stellt Source-Map-Lookups pro Macro bereit.
   - Die Zuordnung laeuft ueber `MACROCATALOG/macros/byName/<macroKey>` auf
     `fileKey` und dann ueber
     `MACROCATALOG/files/byKey/<fileKey>/sourceMap`.
   - `mrvmRuntimeCatalogSourceMapForMacro(...)` liefert die source spans eines
     Macros.
   - `mrvmRuntimeCatalogFirstSourceMapSpanForLine(...)` bindet einen line
     breakpoint an den ersten Source-Map-Span dieser Zeile.
   - Der Lookup schreibt keine Breakpoints und erzeugt keine Debugger-Session.
     Breakpoint-Zustand gehoert in spaeteren Slices unter `MACRODEBUGGER`.

   Slice 2c:

   - Line-Breakpoints liegen unter
     `MACRODEBUGGER/breakpoints/byMacro/<macroKey>/byLine/<line>`.
   - Der Breakpoint-Key wird aus dem normalisierten Macro-Key und der
     UI-Source-Line gebildet; die Bindung auf Bytecode erfolgt ueber
     `mrvmRuntimeCatalogFirstSourceMapSpanForLine(...)`.
   - Ein Breakpoint-Eintrag enthaelt:
     - `enabled`
     - `line`
     - `sourceStartOffset`
     - `sourceEndOffset`
     - `bytecodeOffset`
     - `debuggableKind`
     - optional `conditionText`
   - Nicht bindbare Zeilen erzeugen keinen geratenen Bytecode-Offset. UI und
     spaetere Command-Handler muessen den Zustand als unbound/disabled sichtbar
     machen.
   - Breakpoints werden nicht unter `EXECSESSIONS`, `MACROCATALOG`, Settings
     oder Workspace-Persistenz gespeichert.

   Slice 2d:

   - `MRVMRuntimeDebugger` stellt read/write/erase fuer line breakpoints unter
     `MACRODEBUGGER` bereit.
   - `mrvmRuntimeDebuggerWriteLineBreakpoint(...)` bindet vor dem Schreiben
     ueber `mrvmRuntimeCatalogFirstSourceMapSpanForLine(...)`.
   - Nicht bindbare Zeilen werden nicht geschrieben.
   - Das C++-Objekt `MRMacroDebuggerBreakpoint` ist nur Transferobjekt und wird
     aus dem K/V Store rebuilt.
   - Keine VM-Step-Hooks, keine Debug-Session-Objekte, keine UI.

   Slice 2e:

   - `MRVMRuntimeDebugger` liefert die Breakpoints eines Macros aus
     `MACRODEBUGGER/breakpoints/byMacro/<macroKey>/byLine`.
   - Aktivierte Breakpoints koennen als sortierte Bytecode-Offset-Liste fuer
     die spaetere VM-Stop-Policy gelesen werden.
   - Die Offset-Liste ist abgeleitet, nicht authoritative. Authoritative bleibt
     der Breakpoint-K/V unter `MACRODEBUGGER`.
   - Keine VM-Ausfuehrung, keine Debug-Session, keine UI.

3. VM Debug Step Core
   - Dateien: `mrmac/MRVM.hpp`, `mrmac/MRVM.cpp`.
   - Ziel: VM bis zu Debug-Stoppunkten laufen lassen, Snapshots fuer IP, call
     stack, locals und globals erzeugen, normale Macro-Ausfuehrung unveraendert
     lassen.

   Slice 3a:

   - `VirtualMachine::executeDebugAt(...)` nutzt die bestehende
     `executeAt(...)`-Schleife und stoppt kooperativ vor einer Instruktion,
     deren Bytecode-Offset in der aktiven Debug-Offset-Liste liegt.
   - `mrvmRunBytecodeDebugAt(...)` ist ein direkter Probe-/Transferpfad fuer
     Bytecode plus Entry-Offset; er startet keine Exec Session und keine UI.
   - Das Result enthaelt Stop-Grund, Instruktions-Offset, Stack-Tiefe,
     VM-Log und read-only Variablen-Snapshot.
   - Stopps passieren nur an VM-Instruktionsgrenzen. Intrinsics, externe I/O
     und deferred UI playback werden nicht mitten in der C++-Ausfuehrung
     unterbrochen.
   - Kein Step Over/Out, kein Resume, keine Session-Parkung, keine
     Snapshot-Persistenz.

   Slice 3b:

   - `VirtualMachine::continueDebug(...)` setzt eine pausierte Debug-VM ab dem
     zuletzt gestoppten Bytecode-Offset fort.
   - Der Resume ueberspringt den aktuellen Breakpoint genau einmal, damit die
     gestoppte Instruktion ausgefuehrt wird und nicht sofort wieder stoppt.
   - Der Parkzustand bleibt VM-lokal: Bytecode, IP, Call-Stack, Return-State
     und Macro-Frame-Name sind mechanische Live-Handles, keine
     `MACRODEBUGGER`-Werte.
   - `MACRODEBUGGER` bleibt fuer user-sichtbare Debugger-Daten zustaendig;
     pausierte VM-Objekte duerfen spaeter nur ueber Exec-Session-Lifetime
     gehalten werden.
   - Keine UI, keine Worker-Lane, keine Snapshot-Persistenz.

   Slice 3c:

   - Debug-Ausfuehrung kann eine `MRMacroExecutionSession` mit Route `Debug`
     erzeugen.
   - Eine an einem Breakpoint pausierte Debug-VM wird als mechanischer
     Live-Handle an die Session-ID gebunden.
   - `mrvmContinueDebugSession(...)` setzt die VM ueber die Session-ID fort und
     publiziert bei terminalem Ende ein normales Execution-Session-Result.
   - Der geparkte VM-Zustand wird nicht als Wert in `EXECSESSIONS` oder
     `MACRODEBUGGER` gespiegelt. Sichtbare Debugger-Snapshots bleiben spaeter
     eigener `MACRODEBUGGER`-Datenzug.
   - Keine UI, keine Worker-Lane, keine Runner-Integration.

4. Registry Debug Start
   - Dateien: `mrmac/MRVM.hpp`, `mrmac/MRVM.cpp`, spaeter
     `mrmac/MRMacroRunner.*`, ggf. App command/router/menu-Dateien.
   - Ziel: Registry-Macro als Debug-Ziel auswaehlen, Debug-Ausfuehrung starten,
     Fehler sichtbar melden.

   Slice 4a:

   - `mrvmStartDebugMacroByName(...)` startet Debugging aus einem geladenen
     Registry-Macro statt aus einem direkten Bytecode-Probe.
   - Der Start loest den normalisierten Macro-Key ueber `MACROCATALOG`, stellt
     residenten Bytecode ueber den bestehenden Ladepfad sicher und startet dann
     die vorhandene `MRMacroExecutionRoute::Debug`.
   - Aktive Breakpoints werden aus `MACRODEBUGGER` als abgeleitete
     Bytecode-Offset-Liste gelesen. Fehlt ein Breakpoint-Zweig, laeuft das
     Macro ohne Breakpoints bis zum naechsten terminalen VM-Zustand.
   - `firstRunPending` wird wie bei normaler Registry-Ausfuehrung verbraucht
     und in die Debug-VM uebergeben.
   - Kein Source-Marker im Macro, kein zweiter Compilerpfad, keine neue Lane,
     keine zweite Registry und keine UI.

5. Bento Debug UI
   - Dateien: `ui/MRBentoBox.hpp`, `ui/MRBentoBoxProjection.cpp`,
     `ui/MRBentoBoxDiagnostics.cpp` oder passender bestehender Bento-Teil, ggf.
     `ui/MRFileEditor/*`.
   - Ziel: bestehende Rollen `Debugger Output`, `Variables`, `Watches`
     befuellen, Source-Ausfuehrungszeile und Breakpoints anzeigen.
   - Erster Toggle-Slice:
     - Debug-Bento merkt den normalisierten Macro-Key.
     - `F9` toggelt den Line-Breakpoint der Source-Cursor-Zeile.
     - UI schreibt nicht selbst in Breakpoint-Daten; sie ruft den VM-Bridge
       gegen `MRVMRuntimeDebugger` und den zentralen Runtime-K/V.
     - Debugger-Output zeigt `set`, `cleared` oder unbound-Fehlertext.

6. Debugger Color Group
   - Dateien: spaeter festlegen; erwartete Beruehrung:
     `config/settings/MRSettingsRuntime.hpp`,
     `config/settings/MRSettingsThemesProfiles.cpp`,
     `config/settings/MRSettingsSnapshotIO.cpp`,
     `config/settings/MRSettingsAssignments.cpp`, `dialogs/MRColorSetup.cpp`
     oder die jeweilige bestehende Color-Setup-Struktur sowie
     `ui/MRFileEditor/*`, `ui/MRBentoBox*`.
   - Ziel: Debugger bekommt eine eigene Color-Gruppe und eigene
     Palette-Slots. Debugger-Panes und Source-Debug-Markierungen duerfen keine
     Message-Line-, Warning-, Error-, Hero-, Diagnostic- oder Statusline-Slots
     wiederverwenden.
   - Strategische Einordnung: dieser Zwischenzug liegt vor weiteren
     Debugger-UI-Slices fuer IP, Watches, Variables und Stack, weil diese
     Oberflaechen sonst falsche semantische Farbcodes einschleppen.
   - Vorgeschlagene Palette-Slots:
     - `kMrPaletteDebuggerBreakpointActive`
     - `kMrPaletteDebuggerBreakpointInactive`
     - `kMrPaletteDebuggerBreakpointUnbound`
     - `kMrPaletteDebuggerWatchpointActive`
     - `kMrPaletteDebuggerWatchpointInactive`
     - `kMrPaletteDebuggerWatchpointError`
     - `kMrPaletteDebuggerInstructionPointer`
     - `kMrPaletteDebuggerExecutionLine`
     - `kMrPaletteDebuggerStackFrame`
     - `kMrPaletteDebuggerValueChanged`
     - `kMrPaletteDebuggerInputActive`
     - `kMrPaletteDebuggerInputError`
   - Default-Farben duerfen initial an bestehende Theme-Werte angelehnt sein,
     aber nur als Default-Kopie bei der Palette-Initialisierung. Runtime- und
     Rendering-Code muessen danach ausschliesslich die Debugger-Slots nutzen.
   - Der aktuelle Breakpoint-Marker im Source-Editor muss von
     `kMrPaletteMessageWarning` auf
     `kMrPaletteDebuggerBreakpointActive` umgestellt werden.
   - Inaktive oder unbound Breakpoints duerfen nicht durch dieselbe Farbe wie
     aktive Breakpoints dargestellt werden. Der erste UI-Slice darf unbound nur
     im Output melden; sobald unbound im Source-Pane sichtbar wird, muss der
     eigene unbound Slot verwendet werden.
   - Settings-/Theme-Persistenz bleibt zentral:
     - keine Debugger-Farbregistry,
     - keine Sidecar-Settings,
     - keine Serialisierung an `settings.mrmac` vorbei,
     - MRSETUP-/Theme-Export und Color-Setup muessen die neue Gruppe
       konsistent fuehren.
   - Geschuetzte Architektur:
     - Die Implementierung beruehrt Settings-/Theme-Persistenz und braucht
       eine eigene Protected-Architecture-Planung vor Codeaenderung.
     - Keine opportunistische Aenderung an `MRSETUP`, `SAVE_SETTINGS` oder
       Settings-Bootstrap innerhalb anderer Debugger-Slices.

7. Watches And Mutation
   - Dateien: `mrmac/mrmac.c`, `mrmac/mrmac.h`, `mrmac/MRVM.cpp`,
     `mrmac/MRVM.hpp`, `mrmac/vm/MRVMRuntimeDebugger.*`,
     `ui/MRBentoBox*`, `dialogs/MRMacroFile.cpp`.
   - Ziel: read-only Watch-Ausdruecke gegen die pausierte Debug-VM und
     typisierte Mutation vorhandener skalare Variablen.
   - Definitionen liegen unter
     `MACRODEBUGGER/watches/byMacro/<macroKey>/byExpression/<expression>`.
     Sie enthalten nur Ausdruck und enabled-Zustand. Werte, Typen und Fehler
     werden aus der Live-VM abgeleitet und nicht persistiert.
   - Der kanonische Compiler erhaelt einen reinen Ausdrucksmodus. Er nutzt die
     bestehende Ausdrucksgrammatik und den echten Local-Typkontext der
     geparkten VM, aber akzeptiert keine Assignments, Procedures, UI-, Datei-
     oder Prozess-Intrinsics. Kein zweiter Parser und kein zweiter Bytecode.
   - Die VM evaluiert diesen Bytecode mit erhaltenem pausierten Local-, Hash-
     und Debug-Zustand. Ein Fehler der Watch ist kein Fehler der Session.
   - Bento Controls: `F7` legt einen Ausdruck an, `Shift+F7` entfernt ihn
     ueber den bestehenden TVision-Input-Dialog. Die Watches-Pane wird nach
     Start, Continue, Step, Stop und Reset aktualisiert.
   - Wertzeilen verwenden `kMrPaletteDebuggerWatchpointActive`; Fehlerzeilen
     verwenden `kMrPaletteDebuggerWatchpointError` ueber die bestehenden
     Editor-Range-Marker.
   - Fehlerfaelle: Syntax, unbekannter oder nicht mehr sichtbarer Name,
     Typ-/Indexfehler, Division durch null, verbotene Intrinsic und fehlende
     Live-Session.
   - Mutation erfolgt per Klick auf eine Wertzeile im Variables-Pane:
     ein rahmenloser `TInputLine` ist bei Skalaren mit dem alten Wert
     vorbelegt; Enter schreibt, Escape verwirft. `int`, `real`, `str` und
     `char` sind typgleich schreibbar. Hash- und Array-Zeilen bieten
     ergonomische Kommandos fuer Einfuegen, Anhaengen, Loeschen und
     Hash-Key-Rename; verschachtelte Werte verwenden denselben Pfad.
   - Das Feld nutzt ausschliesslich `kMrPaletteDebuggerInputActive`; ein
     abgewiesener Wert bleibt im Feld und nutzt
     `kMrPaletteDebuggerInputError`. Die UI besitzt keinen schreibbaren
     Variables-Shadow: Scope, Typ und Existenz werden beim VM-Write erneut
     validiert; Closure-, Session- und App-Global-Werte verwenden ihre
     bestehenden K/V-Backing-Stores.

8. Workspace Debug Configuration Restore
   - Dateien: spaeter festlegen; erwartete Beruehrung:
     `ui/MRBentoBox*`, `app/commands/MRWindowCommands.cpp`,
     Workspace-/`settings.mrmac`-Serialisierung und Debugger-K/V-Bruecke.
   - Ziel: Debug-Bentos nach Workspace Load als kalte, arbeitsfaehige
     Debug-Konfiguration wiederherstellen, ohne eine alte Live-Session zu
     behaupten.
   - Persistiert werden darf nur Debug-Konfiguration:
     - Debug-Bento-Marker statt reinem Bento-Layout-Zufall,
     - normalisierter Macro-Key,
     - Macro-Display-Name,
     - Source-Pfad oder Source-Spec,
     - bestehendes Bento-Layout ueber `MRBentoWorkspaceSnapshot`,
     - Breakpoint-Konfiguration pro Macro-Key: enabled, Source-Line,
       optional spaeter conditionText und Source-Fingerprint,
     - Watch-Ausdruecke, nicht deren Werte,
     - Cursor und Viewport ueber vorhandene Workspace-Fensterdaten.
   - Nicht persistiert werden:
     - Session-ID,
     - geparkter VM-Handle,
     - Instruction Pointer,
     - Call Stack,
     - Locals-/Variables-Snapshot,
     - Bytecode-Offsets als authoritative Zustand,
     - Source-Map als dauerhafte Wahrheit,
     - Debugger-Output als Live-Wahrheit,
     - paused/running/completed als wiederbelebbarer Live-Status.
   - Restore-Semantik:
     - Workspace Load erzeugt einen Debug-Arbeitsplatz im Zustand
       `Debug config restored, no live session`.
     - `Continue` ist in diesem Zustand ungueltig.
     - Der gueltige Startpunkt ist `Start Debug` oder `Restart Debug`.
     - Beim Neustart wird das Macro-File geladen, mit Source-Map kompiliert,
       persistierte Breakpoint-Konfiguration wird gegen die neue Source-Map
       rebunden und wieder unter `MACRODEBUGGER` in den zentralen Runtime-K/V
       geschrieben.
   - Invarianten:
     - Keine Wiederbelebung alter VM-Objekte.
     - Keine zweite Registry fuer Debug-Konfiguration.
     - Runtime-authoritative Breakpoints bleiben nach Restore
       `MACRODEBUGGER`.
     - Source-Maps bleiben generierte Debug-Start-Daten und werden nicht als
       persistente Wahrheit gespeichert.
     - Workspace-/Settings-Persistenz bleibt ein separater geschuetzter
       Ausbauzug mit eigener Planung.

9. Addendum: Workspace Dirty Flag Audit
   - Dateien: spaeter festlegen; erwartete Beruehrung:
     `app/commands/MRWindowCommands.cpp` und Workspace-dirty Call-Sites.
   - Ziel: Window-Move darf `Workspace autosave marked dirty` nicht massenhaft
     ohne echte Zustandsaenderung ausloesen.
   - Jede Aenderung des Workspace-dirty Flags wird im `mr.log`
     protokolliert: Quelle/Call-Site, vorher/nachher, autosave/preserve und
     falls verfuegbar Window-Id und Geometrie.
   - Kein Eingriff in `settings.mrmac`, `MRSETUP`, `SAVE_SETTINGS` oder
     Workspace-Serialisierung ohne eigene Protected-Architecture-Planung.
   - Keine zweite Dirty-Registry; die bestehende Dirty-Autoritaet bleibt
     erhalten.

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
- Locals/Closure/Session/App Globals im Variables-Pane; kein erfundener
  File-Global-Scope
- vollstaendige rekursive Hash-/Array-Darstellung und strukturelle Mutation
- foreground UI-thread macro execution
- foreground `DELAY` yield, resume and cancel, falls beruehrt
- background-safe `Lane::Macro` execution, falls beruehrt
- staged macro execution, conflict rejection and successful commit, falls
  beruehrt
- deferred UI playback ordering and batching, falls beruehrt
- UI: Bento Pane placement, focus traversal, resize, redraw, modal open/close
- UI: Debugger-Farbgruppe, aktive/inaktive/unbound Breakpoints, IP,
  Watchpoints und geaenderte Werte ohne Message-Line-Farbcodes
- Coprocessor/deferred UI checks, falls worker route oder playback beruehrt wird

## Entschiedene Punkte

- Es gibt keine separate Debugger-Vertragsdatei; die Entscheidungen sind in
  Language-, Execution-Session-, Deferred-UI- und Persistence-Vertrag
  integriert.
- Die Debug-Ausfuehrung folgt dem normalen Profil: UI-Pump, Background oder
  staged Background.
- MRMAC besitzt keine File Globals. Sichtbar sind Locals, Closure-, Session-
  und App-Globals.
- Die interne Source-Identitaet ist normalisierter aufgeloester Pfad plus
  Macro-Name; die UI zeigt `Filename^Makroname`.
- Intrinsics, externes I/O und deferred playback sind nicht step-in-faehige
  Blackboxes.
- Collection-Snapshots haben keine kuenstliche Groessengrenze.
- Pro Source-Identitaet ist genau ein Debugger-Bento zulaessig.
- Ein nicht mehr bindbarer Breakpoint bleibt sichtbar, wird geloggt und bei
  spaeterem Debug-Start erneut gebunden; seine eigene Farbe liegt in der
  Setup-Gruppe Debugger.
- Workspace-Restore erzeugt einen kalten Debugger mit Layout, Source,
  Breakpoint-Definitionen und Watches. Alles, was ein neuer Debug-Lauf erzeugt,
  bleibt unpersistiert.
- Das Schliessen des Debugger-Fensters invalidiert dessen Runtime-Anteile im
  zentralen K/V Store; laeuft ein Worker, erfolgt die Bereinigung kooperativ
  nach dessen Ende.

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
