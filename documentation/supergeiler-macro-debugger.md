# Macro Debugger Plan

## Ziel

MR soll einen source-genauen Debugger fuer Macros aus der Macro-Registry bekommen:

- Macro aus der Registry zum Debuggen starten.
- Source-genaue Breakpoints setzen und treffen.
- Step Into, Step Over, Step Out, Continue, Pause und Stop unterstuetzen.
- Variablen beobachten, zunaechst read-only.
- Locals, file globals und weitere globale Runtime-Bereiche in einem aufklappbaren Variables-Pane darstellen.
- Die bestehenden BentoBox-Panes frei platzierbar weiterverwenden.

Nicht-Ziel: Bytecode-Ansicht als Benutzerfunktion. Bytecode kann intern fuer Diagnose nuetzlich sein, ist aber keine UX fuer Macro-Debugging.

## Protected Architecture

Protected architecture touched: yes.

Betroffene Vertraege:

- `documentation/architecture/mrmac-language-contract.md`
- `documentation/architecture/vm-tvcall-contract.md`
- `documentation/architecture/coprocessor-deferred-ui-contract.md`
- `documentation/architecture/tvision-integration-contract.md`
- `documentation/architecture/build-regression-contract.md`

Betroffene geschuetzte Bereiche:

- `mrmac/mrmac.c` und `mrmac/mrmac.h`: source map fuer canonical bytecode generation.
- `mrmac/MRVM.cpp` und `mrmac/MRVM.hpp`: debugfaehige Ausfuehrung, Instruction-Breaks, Stack/Variablen-Snapshots.
- `mrmac/MRMacroRunner.cpp` und `mrmac/MRMacroRunner.hpp`: Debug-Start aus Registry-Macros.
- `coprocessor/MRCoprocessor*` und `coprocessor/MRCoprocessorDispatch*`: falls Debug-Ausfuehrung workergefuehrt oder gepumpt wird.
- `ui/MRBentoBox*`: Debugger Output, Watches, Variables und source-nahe Marker-Projektion.

Invarianten:

- Kein zweiter Compiler-Frontend.
- Keine Opcode-Umnummerierung.
- Keine direkten TVision-Screenbuffer-Writes.
- Kein Render-Seitenkanal fuer Debugger-Overlay.
- TVCALL und Macro-Screen-Operationen bleiben staged/projection-basiert, soweit sie nicht bereits UI-thread-only laufen.
- Breakpoints bleiben im ersten Entwurf ephemeral pro Debug-Session und werden nicht in Settings oder Workspace persistiert.

## Begriffsklaerung

### Staged Macros

Staged Macros sind die bestehende Ausfuehrungsroute fuer Macros, die nicht direkt auf dem UI-Thread laufen muessen, aber Editor-/UI-Zustand beruehren koennen.

Die Route arbeitet konzeptionell so:

1. Der UI-Thread erzeugt einen Snapshot des relevanten Editor-/Runtime-Zustands.
2. Die VM laeuft im Hintergrund gegen diesen Snapshot.
3. Editor-Aenderungen werden als `StagedEditTransaction` gesammelt.
4. UI-Kommandos werden als deferred UI commands gesammelt.
5. Der UI-Thread prueft beim Abschluss auf Konflikte.
6. Nur bei kompatiblem Live-Zustand werden Transaction und deferred UI playback angewendet.

Das ist kein Debugger-Feature, sondern ein vorhandenes Nebenlaeufigkeits- und Commit-Modell. Ein Debugger darf dieses Modell nicht nebenbei umbauen.

### Paused/Staged Mode

Ein paused/staged Debugger-Modus waere ein hypothetischer Modus, in dem ein Macro beim Debuggen nicht direkt reale Editor-/UI-Side-Effects ausfuehrt, sondern Aenderungen bis zur Freigabe sammelt oder als Vorschau haelt.

Das klingt attraktiv, waere aber eine andere Semantik als normales Debuggen:

- Breakpoints koennten vor noch nicht committed Side-Effects stehen.
- Variablen und Editorzustand waeren nicht mehr eindeutig "live".
- TVCALL/deferred UI muesste in einer Vorschauwelt gespiegelt werden.
- Step Into ueber `RUN_MACRO(...)` wuerde verschachtelte staged Welten erzeugen.

Planentscheidung: Der erste Macro Debugger fuehrt echte Macro-Semantik aus und pausiert kooperativ an VM-Instruktionsgrenzen. Ein preview-/staged-debug mode bleibt explizit ausserhalb des ersten Plans.

## UX

Der Debugger ist keine separate Karte innerhalb der BentoBox. "Debugger" im Kanban-Sinn meint das Feature.

Die BentoBox bleibt frei platzierbar:

- Source Pane: vorhandener Source-Editor mit Breakpoint-Markern und aktueller Ausfuehrungszeile.
- Debugger Output: Debug-Status, Treffer, Fehler, Stop/Pause-Meldungen.
- Variables: aufklappbare Gruppen.
- Watches: spaeter explizite Ausdruecke, zunaechst optional leer oder nicht aktiviert.

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

Die Debugger-Controls sollen nicht direkt rendern. Sie aendern Debugger-Zustand; BentoBox und Editor-Projektion zeichnen danach TVision-konform.

## Debug Target

Das Debug-Ziel ist ein Macro aus der Registry.

Begruendung:

- Der Debugger arbeitet auf dem vom Runtime-System bekannten Macro-Begriff.
- Dateien mit mehreren Macros muessen nicht als Primaer-UX geloest werden.
- `RUN_MACRO(...)` und gebundene Registry-Macros koennen spaeter konsistent in Step Into einbezogen werden.

Erforderlich ist trotzdem eine Source-Referenz:

- Registry-Eintrag muss auf Ursprungspfad und Macro-Name abbildbar sein.
- Die Source-Map muss pro kompilierter Macro-Datei mehrere Macros unterscheiden koennen.
- Der Debugger startet auf dem gewaehlten Macro-Entry.

## Source Map

Source-genaues Debugging ist Pflicht.

Der Compiler muss beim canonical bytecode generation path eine Source-Map erzeugen:

- bytecode offset / instruction start
- source file path oder source identity
- source line
- source column oder source offset, soweit stabil verfuegbar
- macro name / macro entry range

Die Source-Map gehoert zur letzten Compile-Operation oder zu einem expliziten Compile-Ergebnis fuer Debugging. Sie darf kein zweiter Compilerpfad werden.

Breakpoints werden source-seitig gesetzt:

- session-lokal
- pro Macro-Source-Identity
- line-basiert im ersten Schritt
- auf naechste debuggable instruction normalisiert

Wenn eine Zeile keine debuggable instruction erzeugt, muss der Debugger das sichtbar behandeln: Breakpoint disabled/unbound oder auf naechste gueltige Zeile gebunden. Keine Bytecode-Heuristik in der UI.

## Debug Session Model

Es braucht eine interne Debug-Session, aber keine Benutzer-Shell.

Die Debug-Session besitzt:

- ausgewaehltes Registry-Macro
- kompilierte Bytecode-Kopie
- Source-Map
- aktuelle VM
- Status: idle, running, paused, stopped, completed, failed
- Breakpoints
- Step-Modus
- aktueller source location snapshot
- variable snapshot
- call stack / macro invocation stack
- letzte Debugger-Meldung

Die UI liest Snapshots. Sie besitzt nicht die VM.

## VM Debugging

Die VM braucht einen debugfaehigen Ausfuehrungspfad:

- Ausfuehrung bis Breakpoint, Step-Bedingung, Pause-Anforderung, Stop-Anforderung, Fehler oder Halt.
- Kooperative Pause/Stop an VM-Instruktionsgrenzen.
- Kein Abbruch mitten in einem C++ intrinsic, externem I/O oder UI playback.
- `DELAY` muss kooperativ pausierbar/stopbar bleiben, soweit der vorhandene Delay-Mechanismus das erlaubt.

Step-Semantik:

- Step Into betritt `CALL` und `RUN_MACRO(...)`.
- Step Over fuehrt Calls bis zur naechsten source line im aktuellen Frame aus.
- Step Out laeuft bis zur Rueckkehr aus dem aktuellen Frame.
- Continue laeuft bis Breakpoint, Pause, Stop, Fehler oder Halt.

Fuer `RUN_MACRO(...)` muss der Debugger verschachtelte Macro-Aufrufe als Debug-Frames modellieren. Wenn ein Ziel-Macro keine Source-Map hat, muss Step Into erklaert abbrechen oder als external frame angezeigt werden.

## Variables Pane

Erster Umfang: read-only Inspector.

Gruppen:

- Locals
- File globals
- App globals
- Weitere Runtime-Globals, falls im VM-Modell vorhanden
- Stack optional nur fuer Diagnose, nicht als Standard-UX

Darstellung:

- Aufklappbare Zeilenueberschriften wie "Locals" und "File globals".
- Eingerueckte Variablen darunter.
- Hashes und Arrays rekursiv aufklappbar, aber budgetiert.
- Detailansicht bei Auswahl einer Variable.

Mutation bleibt spaeterer Slice. Mutation muss typisiert, validiert und an VM-Speichergrenzen gebunden sein. Besonders Hashes, Arrays und globale Runtime-Stores duerfen nicht ueber UI-Schattenzustand mutiert werden.

## Threading And Pumping

Der Debugger darf den UI-Thread nicht durch lange Macro-Ausfuehrung blockieren.

Zulaessige Richtung:

- VM laeuft budgetiert oder workergefuehrt.
- Debug-Events und Snapshots werden kontrolliert an den UI-Thread uebergeben.
- UI zeichnet nur TVision-konform ueber vorhandene Views/Panes.

Nicht zulaessig:

- direktes Rendering an TVision vorbei
- eigenes Overlay mit Screenbuffer-Zugriff
- versteckte UI-Side-Effects aus generischen VM-Intrinsics
- neue deferred playback boundaries als Nebeneffekt

## Implementation Slices

### Slice 1: Architecture Decision And Source Map Plan

Dateien:

- `documentation/supergeiler-macro-debugger.md`
- spaeter ggf. Architektur-Entscheidungsnotiz nach Maintainer-Entscheid

Ziel:

- Geschuetzte Architekturentscheidung fuer Source-Map und Debug-Ausfuehrung festlegen.
- Keine Codeaenderung an VM/Compiler/UI.

### Slice 2: Compiler Source Map

Dateien:

- `mrmac/mrmac.c`
- `mrmac/mrmac.h`
- ggf. `mrmac/MRMacroRunner.*`

Ziel:

- Source-Map im canonical compiler path erzeugen.
- Macro entries source-genau zuordnen.
- Keine Grammar- oder Opcode-Semantik aendern.

### Slice 3: VM Debug Step Core

Dateien:

- `mrmac/MRVM.hpp`
- `mrmac/MRVM.cpp`

Ziel:

- VM bis zu kontrollierten Debug-Stoppunkten laufen lassen.
- VM-Snapshots fuer IP, call stack, locals und globals erzeugen.
- Bestehende normale Macro-Ausfuehrung semantisch unveraendert lassen.

### Slice 4: Registry Debug Start

Dateien:

- `mrmac/MRMacroRunner.*`
- ggf. App command/router/menu-Dateien

Ziel:

- Registry-Macro als Debug-Ziel auswaehlen.
- Debug-Ausfuehrung statt normaler Run-Route starten.
- Fehler sichtbar melden, ohne Dialog-/Settings-Pfade zu verbiegen.

### Slice 5: Bento Debug UI

Dateien:

- `ui/MRBentoBox.hpp`
- `ui/MRBentoBoxProjection.cpp`
- `ui/MRBentoBoxDiagnostics.cpp` oder passender bestehender Bento-Teil
- ggf. `ui/MRFileEditor/*` fuer source marker projection

Ziel:

- Bestehende Rollen `Debugger Output`, `Variables`, `Watches` funktional befuellen.
- Source-Ausfuehrungszeile und Breakpoints anzeigen.
- Keine neue UI-Architektur und kein Overlay-Hack.

### Slice 6: Watches And Mutation

Dateien:

- erst nach Review von Slice 2-5 festlegen

Ziel:

- Watch-Ausdruecke.
- Spaeter typisierte Variable-Mutation.

## Deliberately Avoided

- Kein Bytecode-Debugger als UX.
- Keine Persistenz von Breakpoints.
- Keine neue generische Pane-/Dialog-Architektur.
- Kein zweiter Compiler.
- Keine direkten Screenbuffer-Writes.
- Kein staged preview debugger im ersten Entwurf.
- Keine Variablenmutation im ersten lauffaehigen Debugger-Slice.

## Regression And Manual Checks

Pflicht bei relevanten Slices:

- `make clean all CXX=clang++`
- representative Macro compile
- VM execution of compiled bytecode
- Source-Map: mehrere Macros in einer Datei, Breakpoint-Zeilen mit und ohne emitted instruction
- Debug run: Continue, Pause, Stop
- Step Into, Step Over, Step Out
- Step Into ueber `RUN_MACRO(...)`
- Locals/file globals/app globals im Variables-Pane
- Hash/Array read-only Darstellung
- TVCALL/deferred UI Macro-Faelle
- UI: Bento Pane placement, focus traversal, resize, redraw, modal open/close in Umgebung
- Coprocessor/deferred UI checks, falls worker route oder playback beruehrt wird

## Open Decisions

- Soll eine separate Architekturvertragsdatei fuer Macro Debugging entstehen, bevor VM/Compiler geaendert werden?
- Soll Debug-Ausfuehrung budgetiert im UI-Pump oder als eigene Coprocessor-Lane laufen?
- Wie exakt werden file globals im vorhandenen Runtime-Modell von app globals abgegrenzt?
- Wie wird ein Registry-Macro eindeutig auf Source-Pfad und Source-Map-Version gebunden?
- Welche Intrinsics gelten beim Debuggen als nicht step-in-faehige black boxes?
- Wie gross darf der Snapshot fuer Hashes/Arrays im Variables-Pane sein?
