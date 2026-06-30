# LaTeX Sprachsupport und LSP - Zugplanung

Stand: 2026-06-30

## Ziel

LaTeX soll in mr gut genug werden, um die mr-Dokumentation selbst in LaTeX zu
setzen und als PDF zu distribuieren. Zielniveau ist nicht nur "Syntaxfarbe
vorhanden", sondern ein alltagstauglicher Editorpfad:

- brauchbares Folding,
- brauchbares Smart Indenting,
- brauchbares Outline,
- stabile LSP-Integration mit digestif und texlab,
- sauber platzierte SideKicks,
- nutzbare Snippets mit korrekten Sprungmarkern,
- keine Regression in C/C++ und anderen bereits trainierten Sprachen.

## Aktueller Stand

Bekannt erledigt:

- LaTeX ist als `MRSyntaxLanguage::Latex` vorhanden.
- LSP-Server-Mapping enthaelt `digestif` und `texlab`.
- LSP-Live-Matrix enthaelt LaTeX-Faelle fuer digestif/texlab.
- LaTeX-Folding/Indent/Outline wurden bereits durch Trainerlaeufe verbessert.
- SideKick-Geometrie wurde generisch korrigiert:
  Mauspositionen werden auf Dokumentoffsets abgebildet und daraus wieder in
  sichtbare View-Koordinaten projiziert.
- Snippet-SideKick-Geometrie beruecksichtigt horizontales Scrolling.

Nicht als abgeschlossen betrachten:

- LaTeX-Akzeptanz gegen echte Dokumente fehlt noch.
- digestif und texlab muessen getrennt bewertet werden.
- Snippet-Qualitaet und Platzhalterpositionen sind noch nicht breit geprueft.
- Folding/Indent/Outline brauchen gezielte LaTeX-Kantenfaelle.
- Build-/Diagnostics-Pfad mit `latexmk` muss im echten Fehlerfall verprobt
  werden.

## Einstieg fuer neuen Kontext

Arbeitsbaum vor Beginn pruefen:

```sh
git branch --show-current
git status --short
```

Zum Zeitpunkt dieser Planung war `main` aktiv und der Arbeitsbaum bereits
dirty. Daher vor Implementierung klaeren, welche Aenderungen bereits gewollt
sind und was zum neuen Arbeitszweig gehoeren soll.

Empfohlener neuer Branch:

```sh
git switch -c latex-quality-pass
```

Nicht blind erstellen, falls bereits ein passender Branch aktiv ist.

## Geschuetzte Architektur

Die eigentliche Umsetzung wird wahrscheinlich geschuetzte Bereiche beruehren.
Vor jedem Implementierungszug explizit berichten:

- Protected architecture touched: yes/no
- betroffene Dateien/Funktionen,
- betroffener Vertrag,
- Invarianten,
- Build- und Regressionsplan.

Wahrscheinlich relevante Vertraege:

- `documentation/architecture/syntax-analysis-contract.md`
- `documentation/architecture/coprocessor-deferred-ui-contract.md`
- `documentation/architecture/tvision-integration-contract.md`
- `documentation/architecture/build-regression-contract.md`
- bei Keybindings/Settings nur falls wirklich noetig:
  `documentation/architecture/keymap-contract.md`,
  `documentation/architecture/settings-runtime-contract.md`,
  `documentation/architecture/settings-persistence-contract.md`

Wichtig: Keine Settings-Serialisierung, kein K/V-Umweg, keine neuen Registries,
keine opportunistische Keymap- oder Persistence-Aenderung.

## Relevante Dateien

LSP und SideKick:

- `app/MRCommandRouter.cpp`
- `app/services/MRLspServerProfile.cpp`
- `app/services/MRLspEditorSource.cpp`
- `app/services/MRLspAppService.cpp`
- `app/services/MRLspServiceSession.cpp`
- `lsp/MRLspCompletion.cpp`
- `lsp/MRLspHover.cpp`
- `lsp/MRLspSignatureHelp.cpp`
- `lsp/MRLspDocumentSymbols.cpp`
- `ui/MRSidekickEditor.cpp`

Folding, Indenting, Outline:

- `ui/MRFileEditor/MRFileEditorFoldWarmup.cpp`
- `ui/MRFileEditor/MRFileEditorIndent.cpp`
- `outline/MROutlineFoldProducer.cpp`
- `derivedstate/MRSyntaxDerivedState.cpp`
- `ui/MRSyntax.cpp`
- `ui/syntax/MRSyntaxMetadata.cpp`

Trainer:

- `trainers/foldtrainer/mrfoldtrainer.cpp`
- `trainers/indenttrainer/mrindenttrainer.cpp`
- `trainers/outlinetrainer/mroutlinetrainer.cpp`

Regressionen/Probes:

- `regression/mr-regression-checks.cpp`
- `regression/mr_lsp_live_matrix_probe.cpp`
- `regression/mr_lsp_server_profile_probe.cpp`
- `regression/mr_lsp_service_session_probe.cpp`
- `regression/mr_lsp_completion_probe.cpp`
- `regression/mr_lsp_hover_probe.cpp`
- `regression/mr_lsp_signature_help_probe.cpp`
- `regression/mr_lsp_document_service_probe.cpp`

Arbeitsmaterial:

- `misc/latex_test.tex`
- `documentation/manuals/mr-userref.tex`
- Screenshots aus dem SideKick-Problem:
  - `/home/idoc/mr/Bildschirmfoto_20260630_183036.png`
  - `/home/idoc/mr/Bildschirmfoto_20260630_183112.png`
  - `/home/idoc/mr/Bildschirmfoto_20260630_183235.png`

## Zug 1 - Baseline und Reproduzierbarkeit

Ziel:

- Ist-Zustand reproduzierbar messen.
- Keine Implementierung.
- Klaeren, welche LaTeX-Server lokal verfuegbar sind.

Kommandos:

```sh
command -v digestif || true
command -v texlab || true
command -v latexmk || true
./regression/mr-regression-checks
./regression/mr_lsp_server_profile_probe
./regression/mr_lsp_live_matrix_probe
```

Falls Probes nicht gebaut sind:

```sh
make regression/mr-regression-checks regression/mr_lsp_server_profile_probe regression/mr_lsp_live_matrix_probe CXX=clang++
```

Erwartung:

- Bestehende Regressionen bleiben gruen.
- Fehlende externe Server duerfen bei Live-Probes zu SKIP fuehren, nicht zu
  falschen Produktivannahmen.
- digestif und texlab getrennt protokollieren.

## Zug 2 - LaTeX LSP Akzeptanzmatrix

Ziel:

- Echte LaTeX-LSP-Faelle statt nur generischer LSP-Pfad.
- digestif und texlab getrennt bewerten.

Faelle:

- Package completion: `\usepackage{ti` -> `tikz`
- Environment completion: `\begin{tab` -> `tabular`
- Begin completion/snippet: `\beg`
- Label/reference: `\label{sec:alpha}` und `\ref{sec:`
- Citation: `\cite{...}` mit minimaler `.bib`
- Section symbols: `\part`, `\chapter`, `\section`, `\subsection`
- Math environments: `equation`, `align`, `cases`
- Table environments: `tabular`, `longtable`
- Verbatim/listing: `verbatim`, `lstlisting`

Akzeptanz:

- Completion landet am richtigen Dokumentoffset.
- SideKick sitzt sichtbar nahe am Token, nicht an der Mausspalte im Leerraum.
- Hover/Signature verdecken nicht den Anker.
- Keine Regression bei `clangd`-Faellen in der Live-Matrix.

Moegliche Erweiterung:

- `regression/mr_lsp_live_matrix_probe.cpp` um LaTeX-spezifische Checks
  erweitern. Vorher explizit freigeben lassen, weil neue Regressionen laut
  Projektregel zustimmungspflichtig sind.

## Zug 3 - Snippets und Platzhalter

Ziel:

- LaTeX-Snippets inklusive Sprungmarker belastbar machen.

Zu pruefen:

- `\begin{environment}...\end{environment}`
- `\section{title}`
- `\subsection{title}`
- `\label{key}`
- `\ref{key}`
- `\cite{key}`
- `itemize/enumerate` mit `\item`
- `tabular` Grundgeruest
- `figure`/`table` mit `\caption` und `\label`
- Math: `equation`, `align`, `cases`

Akzeptanz:

- Ersetzter Bereich ist exakt.
- Platzhalter liegen auf sichtbaren, editierbaren Textbereichen.
- Vorwaerts-/Rueckwaerts-Sprung besucht keine Kantenmarker als echte Felder.
- Commit ersetzt nur den vorgesehenen Bereich.
- Horizontal gescrollte und kurze Kommandos bleiben korrekt positioniert.

Betroffene Stellen:

- `app/MRCommandRouter.cpp`
- `ui/MRSidekickEditor.cpp`
- Middleware-Makros wie `digestif-snippets.mrmac`, falls vorhanden oder
  wieder einzufuehren.

## Zug 4 - Folding

Ziel:

- LaTeX-Folding strukturell sinnvoll und konservativ.
- Falsches Folding ist schlechter als fehlendes Folding.

Aktueller Schwerpunkt:

- `ui/MRFileEditor/MRFileEditorFoldWarmup.cpp`
- Funktionen um:
  - `latexHeadingLevel`
  - `latexLineBeforeComment`
  - `parseLatexEnvironmentCommand`
  - `parseLatexLeadingBeginEnvironment`
  - `parseLatexLeadingEndEnvironment`
  - `computeFoldSpansForLineTexts`

Faelle:

- Section-Hierarchie:
  `\part`, `\chapter`, `\section`, `\subsection`, `\subsubsection`,
  `\paragraph`, `\subparagraph`
- Starred variants: `\section*{...}`
- Optional argument: `\section[Short]{Long}`
- Environments:
  `document`, `itemize`, `enumerate`, `tabular`, `figure`, `table`,
  `equation`, `align`, `cases`, `verbatim`, `lstlisting`
- Same-line begin/end darf keinen falschen Langbereich erzeugen.
- Kommentierte `\begin`/`\end` duerfen nicht wirken.
- Escaped percent `\%` darf Kommentarerkennung nicht abbrechen.
- Unbalancierte Environments konservativ behandeln.

Trainer:

```sh
./trainers/foldtrainer/mrfoldtrainer --language=latex input.tex output.fold.txt
```

Akzeptanz:

- Section-Folds enden vor gleich- oder hoeherwertiger Folgesektion.
- Environment-Folds enden am passenden `\end{...}`.
- Verbatim/listing wird nicht durch enthaltene LaTeX-Kommandos zerlegt, sofern
  mit einfachen Regeln erkennbar.
- C/C++ Folding Regressionen bleiben gruen.

## Zug 5 - Indenting

Ziel:

- Smart Indenting fuer LaTeX stabilisieren, ohne C/C++-Indent zu beschaedigen.

Betroffene Datei:

- `ui/MRFileEditor/MRFileEditorIndent.cpp`

Faelle:

- Nach `\begin{itemize}` Einrueckung fuer `\item`.
- Innerhalb `itemize/enumerate` konsistente `\item`-Ausrichtung.
- Nach `\begin{tabular}` keine C-artige Klammerlogik anwenden.
- `align`, `cases`, `equation` konservativ behandeln.
- `\end{...}` dedentet auf passenden `\begin{...}`-Level.
- Kommentarzeilen im Environment behalten plausiblen Level.
- Verbatim/listing nicht smart umformen.
- Tabs/Spaces gemaess Editor-Settings beachten.

Trainer:

```sh
./trainers/indenttrainer/mrindenttrainer --language=latex --indent-style=smart --tab-expand=on input.tex output.indent.txt
./trainers/indenttrainer/mrindenttrainer --language=latex --indent-style=all --ui-indent-style=all --tab-expand=all input.tex output.indent.matrix.txt
```

Akzeptanz:

- Keine aggressiven Reindents grosser Dokumentbereiche.
- Enter an typischen Positionen erzeugt erwartbaren Fill.
- Smart Dedent fuer `\end{...}` funktioniert.
- Bestehende Tabstop-/Indent-Regressionschecks bleiben gruen.

## Zug 6 - Outline

Ziel:

- Outline zeigt LaTeX-Dokumentstruktur sinnvoll.

Betroffene Stellen:

- `outline/MROutlineFoldProducer.cpp`
- `ui/MRFileEditor/MRFileEditorFoldWarmup.cpp`
- `trainers/outlinetrainer/mroutlinetrainer.cpp`

Faelle:

- `\part`
- `\chapter`
- `\section`
- `\subsection`
- `\subsubsection`
- starred variants
- optional short titles
- `\appendix`
- labels als Zusatzinfo, aber nicht als eigene Hauptstruktur, ausser spaeter
  explizit gewuenscht

Trainer:

```sh
./trainers/outlinetrainer/mroutlinetrainer --language=latex input.tex output.outline.txt
```

Akzeptanz:

- Reihenfolge entspricht Dokument.
- Level entsprechen LaTeX-Hierarchie.
- Optionalargumente zerstoeren Titel nicht.
- Kommentare werden ignoriert.

## Zug 7 - Build und Diagnostics

Ziel:

- LaTeX-Dokumente lassen sich ueber bestehenden Build-Pfad sinnvoll bauen.
- Fehlerdiagnostik ist brauchbar.

Betroffene Stellen:

- `app/commands/MRExternalCommand.cpp`
- Compilerprofile/Settings nur lesen, nicht opportunistisch umbauen.
- SideKick fuer Compilerdiagnostics: `ui/MRBentoBoxDiagnostics.cpp`

Faelle:

- Erfolgreicher `latexmk`-Build.
- Syntaxfehler mit LaTeX-Log.
- Missing package.
- Undefined reference/citation.
- Mehrfachlauf-Hinweise.

Akzeptanz:

- Kein dauernder K/V-Zugriff.
- Kein UI-Lag.
- Buildfenster und Diagnostic-SideKick bleiben bedienbar.
- Keine neue Settings-Serialisierung ohne ausdrueckliche Freigabe.

## Zug 8 - Dokumentations-Akzeptanz

Ziel:

- Das reale mr-Handbuch wird zum Akzeptanzdokument.

Datei:

- `documentation/manuals/mr-userref.tex`

Vorgehen:

- Datei in mr oeffnen.
- Folding pruefen.
- Outline pruefen.
- Completion/Hover an typischen LaTeX-Stellen pruefen.
- `latexmk`-Build aus mr starten.
- PDF-Erzeugung pruefen.

Akzeptanz:

- Das Handbuch ist ohne Editorfrust bearbeitbar.
- Die Strukturansicht hilft bei Navigation.
- LSP stiftet Nutzen und verursacht kein sichtbares Seitenrauschen.

## Externe Testdaten

Bei Internetnutzung wegen Aktualitaet/Verfuegbarkeit browsen oder per `git`
gezielt laden. Kleine Stichprobe reicht, aber unterschiedliche Dokumenttypen
waehlen:

- article
- book/report mit chapter
- beamer
- math-lastiges Dokument
- tabellenlastiges Dokument
- bib/citation-lastiges Dokument
- dokument mit verbatim/listings
- TikZ-Dokument
- deutschsprachiges Dokument mit Umlauten
- groesseres Projekt mit mehreren Dateien

Nicht den Trainer mit massiven Repositories fluten. Dateien einzeln und
kontrolliert verwenden. RAM-Verbrauch beobachten:

```sh
/usr/bin/time -v ./trainers/foldtrainer/mrfoldtrainer --language=latex input.tex output.fold.txt
/usr/bin/time -v ./trainers/indenttrainer/mrindenttrainer --language=latex --indent-style=smart input.tex output.indent.txt
/usr/bin/time -v ./trainers/outlinetrainer/mroutlinetrainer --language=latex input.tex output.outline.txt
```

## Regression und Build

Vor Uebergabe jeder Implementierung:

```sh
git diff --check
make clean all CXX=clang++
./regression/mr-regression-checks
```

Zusaetzlich, wenn gebaut/verfuegbar:

```sh
./regression/mr_lsp_server_profile_probe
./regression/mr_lsp_live_matrix_probe
./regression/mr_lsp_service_session_probe
```

Falls externe LSP-Server fehlen, SKIP sauber berichten.

Pflichtbericht:

- Decision
- Files
- Change
- Build
- Warnings
- Regression
- Manuelle Verprobung
- Naechster Zug

## Nicht tun

- Keine generische neue "LaTeX Engine" erfinden.
- Keine neuen Parser-Frameworks.
- Keine neuen globalen Registries.
- Keine Settings neben dem zentralen K/V Store.
- Keine SideKick-Sondergeometrie nur fuer LaTeX, wenn der generische
  View-/Offset-Pfad korrekt sein muss.
- Keine C/C++-Heuristiken fuer LaTeX wiederverwenden, wenn sie nur zufaellig
  passen.
- Keine grossen Trainer-Resultate oder heruntergeladenen Repositories ins Repo
  committen.
- Keine neuen Regressionen ohne ausdrueckliche Freigabe hinzufuegen.

## Offene Entscheidungen fuer Dr. Raus

- Soll digestif oder texlab die bevorzugte LaTeX-Default-Empfehlung sein?
- Sollen Snippet-Middleware-Makros fuer LaTeX wieder defaultmaessig
  vorgeschlagen werden, sobald MRExpand/Snippet-Middleware stabil ist?
- Sollen Labels/Citations im Outline erscheinen oder nur Sections?
- Soll `\appendix` im Outline sichtbar sein?
- Welche Mindestqualitaet gilt fuer beamer?
- Soll `latexmk` Build-Output spaeter strukturiert geparst werden oder zunaechst
  nur als normaler Build-Log dienen?

## Definition of Done

LaTeX Sprachsupport gilt erst dann als abgeschlossen, wenn:

- `mr-userref.tex` in mr praktisch bearbeitbar ist,
- Folding, Indent und Outline an mindestens zehn unterschiedlichen LaTeX-Dateien
  plausibel sind,
- digestif und texlab entweder bestanden oder begruendet abgestuft sind,
- SideKick-Positionierung bei LaTeX, C und C++ gruen ist,
- Snippet-Platzhalter fuer die wichtigsten LaTeX-Snippets korrekt springen,
- `latexmk`-Build aus mr fuer Erfolg und Fehlerfall verprobt ist,
- `make clean all CXX=clang++` ohne Warnungen/Fehler laeuft,
- die relevanten Regressionen gruen sind.
