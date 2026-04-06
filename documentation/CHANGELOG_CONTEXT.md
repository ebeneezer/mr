# CHANGELOG_CONTEXT

Laufendes Projektprotokoll fuer Kontextwechsel und schnelle Wiederaufnahme.

## Warmup-Regel
- Verbindliche Regeln stehen ausschliesslich in `documentation/Codex Kontext Übergabe.yaml` (Single Source of Truth).
- Diese Datei ist ein Verlaufsprotokoll und keine normative Regelquelle.
- Warmup-Reihenfolge: zuerst YAML, dann dieses Changelog, danach `MRMAC_V1_STATUS.md`.


## 2026-04-06
- Neue verbindliche TVision-Standardregel aufgenommen:
  - Standardnaehe zu magiblot/TVision ist zwingend.
  - Keine nicht standardnahen UI-Workarounds oder Overlay-Konstruktionen, wenn eine TVision-nahe Loesung moeglich ist.
  - Dieses Projekt optimiert nicht auf Geschwindigkeit der Fertigstellung.
  - Prioritaet hat technische Exzellenz, saubere Architektur und robuste, standardnahe Implementierung.

## 2026-04-04
- Neue verbindliche Coding-Style-Regel aufgenommen:
  - `strict_no_guard_backpacks` ist aktiv.
  - Keine symptomatischen Guard-Workarounds ("Guard-Rucksaecke").
  - Fehler werden an der Ursache behoben.
  - Guards sind nur fuer echte externe Randfaelle zulaessig (IO, Benutzerinput, OS/API-Vertraege), nicht zur Kaschierung interner Logikfehler.
- Neue harte MRMAC-Dispatch-Stilregel aufgenommen:
  - Lange String-Dispatch-Ketten in Parser-/Compiler-Pfaden (if/else oder OR-verknuepfte `strcasecmp`) sind untersagt.
  - Verbindlich ist ein tabellengetriebenes Dispatch-/Signaturmodell mit zentraler Arity-/Typvalidierung.
  - Neue Builtins/Intrinsics/Statements wachsen nur ueber Tabelleneintraege und gemeinsame Helfer.
- Refactor-Trigger aufgenommen:
  - Bei mehr als drei Name-Vergleichsaesten oder duplizierter Arity-/Typpruefung ist vor weiterem Feature-Ausbau auf das zentrale Tabellenmodell zu refaktorieren.
- Neue If-Ketten Schwellwertregel:
  - Ab mehr als 5 vergleichbaren if/else-Zweigen ist Refactor verpflichtend.
  - `switch` nur bei gemeinsamem Diskriminator (Token/Enum), String-Dispatch bleibt tabellengetrieben.

## 2026-04-03
- Ordnerstruktur bereinigt:
  - `runtime/` als Source-Ordner aufgeloest.
  - `MRPerformance.*` und `MRCoprocessorDispatch.*` nach `coprocessor/` verschoben.
  - Build/Includes/Abhaengigkeiten auf `coprocessor/*` umgestellt.
- Neue Kontextregel aufgenommen:
  - Vor dem Anlegen neuer Source-/Code-Ordner immer erst Rueckfrage an den User mit kurzer Begruendung.

## 2026-04-02
- TVision-Quellverwaltung auf `git subtree` umgestellt:
  - `tvision-upstream` Remote aktiv.
  - Subtree-Import unter `./tvision` erfolgt.
  - Veraltete TVision-Planungsdateien entfernt und ersetzt durch `documentation/tvision-subtree-workflow.md`.
  - `.vendor-cache` aus dem Workspace entfernt; Vendor-Cache-Pfad ist nicht mehr Teil des Standard-Workflows.
- Neuer Ein-Befehl-Sync fuer TVision:
  - Script: `misc/tvision-sync-safe.sh`.
  - Make-Targets: `make tvision-sync-safe` und `make tvision-status`.
  - Ablauf: stash -> fetch -> subtree pull -> patch queue -> stash restore.
- MRMAC-Kontextregel verschaerft:
  - `documentation/MRMAC_V1_STATUS.md` ist bei Kontextwechsel verbindlicher Warmup-Bestandteil.
- Setup/UI-Korrekturstand nach Regressionen:
  - `CURSORVISIBILITY` vollstaendig entfernt (kein Legacy-Fallback):
    - entfernt aus `Edit settings` Dialog, Settings-Serialisierung, `MRSETUP`-Whitelist und Runtime-Cursorumschaltung.
    - `settings.mrmac` Auto-Create schreibt den Key nicht mehr; Regression prueft explizit auf Abwesenheit.
  - Dialog-Hintergrund repariert:
    - Setup-Dialog-Content nutzt zentrale non-buffered Content-Group (`createSetupDialogContentGroup`), damit Dialogflaechen nicht schwarz ueberzeichnet werden.
  - Dialog-Scrollview-Geometrie vereinheitlicht:
    - Scrollbars auf dem Dialograhmen (analog Textfenster), nicht innerhalb der Contentflaeche.
  - Textfenster-Resize gehaertet:
    - `TMREditWindow::changeBounds` layoutet Editor-Chrome (H/V-Scrollbar + Indicator + Editor-Bounds) explizit nach.
    - `TMRFileEditor` synchronisiert Scrollbar-Sichtbarkeit zusaetzlich mit `sfActive|sfSelected`.
  - PTY-Selbsttest durchgefuehrt:
    - Interaktiv: `Other -> Installation and setup -> Paths` und `Edit settings` geoeffnet.
    - Verifiziert: Cursor-Option nicht mehr vorhanden.
    - Starttests mit mehreren Terminalgroessen (u. a. `80x24`, `173x42`, `170x45`) ohne fehlende Scrollbars beim Initialaufbau.
- UI-Qualitaetsgate als feste Vorgabe verankert:
  - Kein Fortschrittsbericht ohne lokalen PTY-Selbsttest der betroffenen Dialoge/Fenster.
  - Pflichtkriterien: saubere Ausrichtung/Abstaende/Zentrierung, kein Clipping, korrekte Scrollbars/Farben, Fokusrahmen nur am fokussierten Objekt.
  - Pflichtgroessen: mindestens `80x25` plus aktuelle Problemgrenzen.
  - Bei Fehlern: zuerst Fix, erst danach weiterer Planfortschritt.
  - Zusaetzlich Null-Toleranz-Regel: UI-Regressionen Prioritaet 0; neue Features erst nach stabiler Darstellung und bestandenem PTY-Test der betroffenen Oberflaechen.

## 2026-04-01
- Doku-/Ablageregel fixiert:
  - Markdown-Dokumente liegen unter `documentation/`.
  - `misc/` ist nur temporaere Muellhalde/Probe-Ablage.
- Workspace-Hygiene als verbindliche Vorgabe aufgenommen:
  - keine Artefakt-/Nebenfile-Muellhalden im Workspace.
- Regression zentralisiert:
  - Legacy-Probes aus `misc` entfernt.
  - zentrale Probe-Datei: `regression/mr-regression-checks.cpp`.
  - v1-Suite nutzt `regression/mr-regression-checks --probe staged-nav|staged-mark-page`.
- TVCALL angepasst:
  - `TVCALL MESSAGEBOX(...)` ist jetzt case-insensitiv kompatibel zu `TVCALL MessageBox(...)`.
- Sprachstand MRMAC gegen Referenzhandbuch (`ME Macro Language Reference Guide.pdf`) abgeglichen:
  - v1 ist bewusst nicht vollstaendig.
  - groesste Luecken: `SCREEN OPERATIONS`, `USER INPUT AND INTERFACE`,
    `FUNCTION KEY LABEL OPERATIONS`, `DOS SHELL OPERATIONS`, `KEY ASSIGNMENTS`.
  - teilweise vorhanden: `OPERATING SYSTEM INTERFACE`, `MISCELLANEOUS`, `SYSTEM VARIABLES`.
- Setup / Paths begonnen und umgesetzt:
  - Subdialog `Installation / Paths` mit Feldern fuer
    `SETTINGSPATH` (settings.mrmac URI), `MACROPATH`, `HELPPATH`, `TEMPDIR`, `SHELLPATH`.
  - Buttons: `Done` (schreibt settings.mrmac, reloadet silent ohne Neustart), `Cancel` (Sicherheitsfrage bei Aenderungen), `Help` (Dummy-Screen).
  - `MRSETUP`-Whitelist erweitert auf `MACROPATH`, `SETTINGSPATH`, `HELPPATH`, `TEMPDIR`, `SHELLPATH`.
  - Runtime-Verbrauch umgestellt:
    - Help-Datei-Aufloesung ueber konfigurierbaren `HELPPATH`.
    - Externer Kommando-Shell-Start ueber konfigurierbaren `SHELLPATH`.
    - Temporaere Record-Makrodateien ueber konfigurierbaren `TEMPDIR`.
- Fokus-/Rahmenregel zentralisiert (nicht dialogweise):
  - TVision-Standard gilt global in `ui/TMRFrame.cpp`.
  - Hintergrund: modale `execView`-Abläufe koennen parallel `sfActive` auf mehr als einem Fenster lassen.
  - Doppelrahmen + Fenster-Controls nur beim fokussierten Objekt (`sfFocused`).
  - `sfActive`, `owner->state` und `sfSelected` werden fuer diese visuelle Darstellungsentscheidung nicht verwendet.
- PTY-Test und Kommunikationsregel:
  - Fix wurde im PTY durch Start von `./mr` und Oeffnen von `Other -> Installation and setup` selbst getestet.
  - Verbindliche Regel: User erst adressieren, nachdem Codex die Aenderung im PTY selbst getestet hat.
- Fokusdarstellung weiter zentralisiert (Modal-Fall):
  - Root cause: In modalen `execView`-Phasen konnten visuell zwei aktive Frames erscheinen.
  - Zentralfix in `ui/TMRFrame.cpp`:
    - Modal-TopView-Gate: Bei aktivem modalen TopView darf nur dieser View als fokussiert aktiv gezeichnet werden.
    - Fokusdarstellung nutzt `sfFocused`; `sfActive` ist fuer rein visuelle Aktivdarstellung in diesem Pfad nicht massgeblich.
  - Zentralfix in `ui/TMREditWindow.hpp`:
    - `setState`-Override triggert `frame->drawView()` bei `sfFocused/sfSelected/sfActive`, damit Fokuswechsel sofort sichtbar konsistent sind.
- Neue UI-Designvorgabe aufgenommen:
  - Dialog-Buttons (`Done/Cancel/Help`) in einer zentrierten Zeile ausrichten (analog Window-List), keine rechts versetzte Help-Position.
- Terminologie-/Vorbelegungsregel aufgenommen:
  - Datei-Felder als `URI` benennen, Ordnerfelder als `path`.
  - Datei-URIs in Dialogen absolut vorbelegen (kein relatives `mr.hlp` als Default).
  - Paths-Dialog labels bereinigt: `Settings macro URI`, `Macro path`, `Temporary path`; Zusatz `(path + filename)` entfernt.
  - Zugehoerige Fehltexte in Path-Validierung ebenfalls auf `URI`/`path` ohne `directory` umgestellt.
- Paths-Generator/Fallbacks korrigiert:
  - `settings.mrmac`-Generator erzeugt MRMAC-konforme Single-Quoted Strings fuer `MRSETUP(...)`.
  - `Done` + silent reload im PTY mit schreibbarer Settings-URI (`HOME=/tmp`) erfolgreich verifiziert.
  - Help-URI-Default ist jetzt absolut vorbelegt (`/home/.../mr.hlp` statt `mr.hlp`).
  - Runtime-Pfadresolver verwendet keine hartkodierten Datei-URI-Literale mehr; Datei-Defaults werden dynamisch aus Umgebung/CWD/Exe-Dir aufgebaut.
- About-Dialog Zitate-Rotation gefixt:
  - Statt naiver Shuffle-Bag nun "unseen bag" mit Seen-Tracking.
  - Ein Zitat wird erst wieder zugelassen, nachdem alle anderen mindestens einmal gezeigt wurden.
  - PTY-Test: Long-Press auf `Done` aktiviert Rotation; mehrere Folgezitate erschienen ohne fruehe Wiederholung.
- Setup-Defaults/Bootstrap vereinheitlicht:
  - Neue zentrale Default-Routine fuer Setup-Pfade/URIs in `services/MRDialogPaths`.
  - Dieselbe Default-Quelle wird von Setup-Dialog und Bootstrap genutzt.
  - Wenn `settings.mrmac` beim Start fehlt, wird sie automatisch mit Default-`MRSETUP(...)` erzeugt.
  - Regression erweitert: `settings.mrmac auto-create on missing file` (in `regression/mr-regression-checks.cpp`).
- Installation-and-Setup Mnemonics:
  - Buttons nutzen jetzt TVision-Mnemonics via `~X~` (u. a. E/D/C/K/M fuer die ersten 5 Themen).
  - Kontextregel ergaenzt: Bei neuen Setup-Topics User nach Mnemonic fragen + begruendete Empfehlung geben.
- Kontextregel praezisiert:
  - Neue Setup-Settings: User wird mit Vorschlag fuer den exakten Key-String in `settings.mrmac` befragt (Schema wie `"SETTINGSPATH"`, `"MACROPATH"`, ...).
  - Neue Dialoge: Mnemonic-/Highlight-Buchstaben werden von Codex autonom vergeben, ohne Rueckfrage.
  - Workspace-Loeschungen: Rueckfragen vor Loeschungen sind auf User-Wunsch deaktiviert.
- Setup-Hauptdialog vervollstaendigt:
  - `Save configuration` ist kein Dummy mehr: schreibt aktuelle Runtime-Settings nach `settings.mrmac`, reloadet silent und schliesst den Setup-Dialog.
  - `Exit Setup` bleibt direkter Ausstieg ohne Speichern.
  - Shortcut-Sichtbarkeit auf Dialog-Buttons erhoeht (helles Rot mit hoeherem Kontrast).
- Edit-Settings Planung fixiert:
  - Option 1 freigegeben: vollstaendiges Keyset fuer Edit-Settings (`PAGEBREAK`, `WORDDELIMS`, `DEFAULTEXTS`, `TRUNCSPACES`, `EOFCTRLZ`, `EOFCRLF`, `TABEXPAND`, `COLBLOCKMOVE`, `DEFAULTMODE`).
  - Bool-Werte fuer Setup-Keys werden als `true/false` gefuehrt (nicht `1/0`).
- Edit-Settings Option 1 umgesetzt:
  - `MRSETUP` um Keyset erweitert: `PAGEBREAK`, `WORDDELIMS`, `DEFAULTEXTS`, `TRUNCSPACES`, `EOFCTRLZ`, `EOFCRLF`, `TABEXPAND`, `COLBLOCKMOVE`, `DEFAULTMODE`.
  - `settings.mrmac`-Generator schreibt neue Keys inkl. Bool-Literale als `true/false`.
  - `Installation and setup -> Edit settings` als echter Dialog umgesetzt (Done/Cancel/Help, Dirty-Check, silent reload).
  - `DEFAULTEXTS` in Open/Load integriert (Fallback-Suche ohne explizite Dateiendung).
  - `DEFAULTMODE` wirkt auf neu erzeugte Fenster und wird nach Reload auf offene editierbare Fenster angewandt.
  - `PAGEBREAK` an Runtime-Navigation (`NEXT_PAGE_BREAK`/`LAST_PAGE_BREAK`) angebunden.
  - Verifiziert via PTY: `make mr` und `make regression-check` (6/6 pass).
  - UI-Korrektur gemaess Original-Referenz: Bool-Settings bleiben nur Serialisierung (`true/false` in settings.mrmac); der Setup-Dialog nutzt TVision-Controls (Checkboxes/RadioButtons) statt Bool-Textfelder.
- Neuer Setup-Layoutstandard festgelegt:
  - Zentraler Profilansatz `compact/relaxed` fuer Setup-Dialoge.
  - `compact` ist der Standard fuer kleine Terminals (80x25-first).
  - Keine doppelten Dialog-Versionen; nur profilabhaengige Geometrie in einer Implementierung.
