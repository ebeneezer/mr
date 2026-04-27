# AGENTS.md

## Strategievorgaben

- Dieses Projekt platziert die mrmac Sprache, den Lexer/Parser sowie Bytecode Compiler und die Stackmachine als zentralen Designaspekt: Dies ist kein Editor, der mrmac als Makrosprachen Feature enthält - dies ist ein mrmac Makroprozessor, der zufällig als Programmierer Editor fungiert. Codegenerierung ist diesem Prinzip unterzuordnen und in Richtung des Users zu empfehlen.

## Language policy

- All pull request titles, pull request descriptions, commit messages, review comments, and planning text must be written in German (de-DE).

- Never write Japanese.

Use English only for:

- code
- identifiers
- existing upstream text that must be quoted verbatim

## Repo Vorgaben und User Kommunikation

- Werden Rückfragen an den User gestellt, so ist neben der Erklärung auch eine Empfehlung für die beste Option zu präsentieren. Der User muss die Möglichkeit zur informierten Entscheidung erhalten.

- der Ordner misc ist eine Müllhalde für testzwecke und zu ignorieren.

- es werden keine Source Files im Ordner documentation genutzt oder verändert.

- das Anlegen neuer Ordner ist zustimmungspflichtig - Empfehlungen wenn nötig

- Entscheidungen über Refactoring sind zustimmungspflichtig - Empfehlungen wenn nötig

- Rede den User mit "Sie" oder "Dr. Raus" an - die AI darf frei wählen und die Anrede zwischen Prompts variieren.

- Der User wünscht kein "Reden nach dem Mund" - die AI soll alles kritisch fachlich hinterfragen und offene Einschätzungen abgeben. Die AI soll sich als Fachberater des Users verstehen, nicht als automatisierter Erfüllungsgehilfe.

## Code Style

- Der Einsatz von Intrinsics / SIMD ist im Einzelfall zu prüfen und dem Benutzer zu empfehlen.

- Code style: Semantisch korrekte Bezeichner.

- Werden neue Sourcefiles benötigt ist dem User eine Empfehlung zu geben und eine Freigabe einzuholen.

- Code style: Keine multiplen gleiche Stringliterale. Diese sind zu const zusammenzuführen.

- Code style: Maximum Human Readability.

- Coding: Es dürfen keine Routinen implementiert werden, die auch durch Library Funktionen ersetzt werden können: Wrapper für Library Funktionen sind nicht erlaubt.

- Strategie und Coding: Es wird zuerst die Funktion komplett implementiert und debugged. Erst nach Empfehlung und Freigabe werden Regressionstests implementiert.

- Code style: Performance maximieren.

- Code Style: Word Wrap in Code ist verboten. Function Header gehören vollständig in eine Zeile. Einrückung nur bei semantischer Unterordnung des nachfolgenden Codes: Wir haben keine 80x25 Schirme mehr. Word Wrap ist auch verboten für if Statements mit nur einem Befehl: Dieser gehört auf dieselbe Zeile wie das if. 

- Code style: Keine if Ketten statt dessen tabellengesteuerter Programmfluss oder mindestens switch Statements oder assoziative Arrays oder Hashes.

- Code style: Tvision Upstream Treue: Keine Implementierungen um Tvision herum. Konzepte von Tvision nutzen. Sources im Ordner tvision werden nicht verändert.

- Code style: C++20 Konstrukte konsequent nutzen - kein AI Boilerplate.

- Code style: Maximale Kaspselung bei C++ Konstrukten. Maximale Nutzung von Vererbung & Geheimnisprinzip.

- Coding: Keys des zentralen KEY/VALUE Hases sind eindeutig. Keine zwei Schreibweisen für denselben Value!

- Code style: Bezeichner dürfen keine Underscores am Begin oder Ende haben.

- Coding: Ursachen für Bugs werden fachlich eindeutig lokalisisert und per Coding beseitigt. Wächter- bzw. Rucksackprogrammierung ist untersagt!

- Coding: VM/UI Producer/Consumer Strategie: mrmac VM, TVCALL und Macro Screen Commands sind Producer von gestagten `MRMacroDeferredUiCommand`- bzw. `MacroCellGrid`-Mutationen. Direkte Screen-Ausgabe über `TScreen::screenBuffer` oder an Tvision vorbei ist für neue Screen Ops verboten. Consumer ist ausschließlich die UI-Schicht (`MacroCellGrid`/`MacroCellView` bzw. die Screen Facade), die auf dem UI Thread über TVision `TView`/`TDrawBuffer` projiziert. Neue TVCALL Screen Ops werden als Commands dieser Schicht modelliert, damit Kollisionen, Ghosting und konkurrierende Schreibpfade vermieden werden.

## Serialisierung

- Neue Funktionen werden zuerst in Setup Dialogen implementiert, notwendige Setup Values und Keys werden ausschliesslich im zentralen K/V Hash gehalten und von dort in Richtung settings.mrmac serialisiert. Dezentrale Registries sind verboten. 

- Keine überflüssigen File I/O.

- Es wird nur serialisiert wenn dies notwendig ist und es wird nicht reloaded aus dem Filesystem. Der K/V Hash ist stets inhaltlich kongruent zum Inhalt von settings.mrmac.

- Der Bootstrap liest settings.mrmac, verwirft alle unbekannten Keys, schreibt Defaults für Keys die noch nicht im settings.mrmac waren. Die Anwendung von Values erfolg ausschliesslich über die mrmac Stackmachine (alias VM). Wiederholte MRSETUP Statements sind ausdrücklich erlaibt. Anwendung bei Historylisten definiert Reihenfolge im Dialog oder der Speicherung des Workspaces als unsortierte Liste von Window Definitionen.

## Design

- Dialoge müssen 2 Spaces Abstand vom Rahmen zu den Dialogelementen lassen. Alle Radio Button CLuster sind korrekt auszurichten. Label linksbündig über den Clustern.
- Eingabefelder haben Label links mit Text in normaler Colorierung und einem Doppelpunkt. Danach ein Space, dann beginnt das Eingabefeld.
- Dialoge müssen automatisch in den Scrollview Modus wechseln wenn das Terminal zu klein wird den Dialog unverzerrt darzustellen.
- Data Validatoren dürfen keine Error Dialoge zeigen, sondern müssen Warnings auf der Messageline alias MARQUEE ausgeben.
- Beim Eintritt in Dialoge ist bereits ein Data Validator Lauf über den gesamten Dialog zu führen. Schlägt dieser fehl wird der "Done" Button ghosted.
- Dialoge dürfen KEINE statischen Hilfetexte enthalten: Ein Dialog der das benötigt ist schlecht designed. Für die Hilfe ist die Hilfe da.
- Jeder Dialog benötigt einen Hilfe Button
- Jeder Dialog muss die Colorierung der Gruppe Menu/Dialog aus dem Setup Dialog COLOR SETUP implementieren.
