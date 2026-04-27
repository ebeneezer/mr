# AGENTS.md

## Strategievorgaben

- Dieses Projekt platziert die mrmac Sprache, den Lexer/Parser sowie Bytecode Compiler und die Stackmachine als zentralen Designaspekt: Dies ist kein Editor, der mrmac als Makrosprachen Feature enthält - dies ist ein mrmac Makroprozessor, der zufällig als Programmierer Editor fungiert. Codegenerierung ist diesem Prinzip unterzuordnen und in Richtung des Users zu empfehlen.

## Language policy

- All pull request titles, pull request descriptions, commit messages, review comments, and planning text must be written in German (de-DE).

- Never write Japanese.

- Rede mich mit "Sie" oder "Dr. Raus" an.

Use English only for:

- code
- identifiers
- existing upstream text that must be quoted verbatim

## Repo und USer Vorgaben

- Werden Rückfragen an den User gestellt, so ist neben der Erklärung auch eine Empfehlung für die beste Option zu präsentieren. Der User muss die Möglichkeit zur informierten Entscheidung erhalten.

- der Ordner misc ist eine Müllhalde für testzwecke und zu ignorieren.

- es werden keine Source Files im Ordner documentation genutzt oder verändert.

- das Anlegen neuer Ordner ist zustimmungspflichtig

- Entscheidungen über Refactoring sind zustimmungspflichtig.

## Code Style

- Intrinsics / SIMD ist im Einzelfall zu prüfen und ggf. zu empfehlen.

- Code style: Semantisch korrekte Bezeichner ohne leading und ohne trailing underscore. Für Methoden und Membder und Klassen. Werden neue Sourcefiles benötigt ist dafür eine Freigabe einzuholen.

- Code style: Keine multiplen gleiche Stringliterale. Diese sind zu const zusammenzuführen.

- Code style: Maximum Human Readability.

- Strategie und Coding: Es wird zuerst die Funktion komplett implementiert und debugged. Erst nach Empfehlung und Freigabe werden Regressionstests implementiert.

- Code style: Performance maximieren.

- Code Style: Word Wrap in Code ist verboten. Function Header gehören vollständig in eine Zeile. Einrückung nur bei semantischer Unterordnung des nachfolgenden Codes: Wir haben keine 80x25 Schirme mehr.

- Code style: Keine if Ketten statt dessen tabellengesteuerte Entscheidungen.

- Code style: Tvision Upstream Treue: Keine Implementierungen um Tvision herum. Konzepte von Tvision nutzen. Sources im Ordner tvision werden nicht verändert.

- Code style: C++20 Konstrukte konsequent nutzen - kein AI Boilerplate.

- Code style: Maximale Kaspselung bei C++ Konstrukten. Maximale Nutzung von Vererbung und Kapselung. Geheimnisprinzip.

- Coding: Keys des zentralen KEY/VALUE Hases sind eindeutig. Keine zwei Schreibweisen für denselben Value!

- Code style: Bezeichner dürfen keine Underscores am Begin oder Ende haben.

## Serialisierung

- Neue Funktionen werden zuerst in Setup Dialogen implementiert, notwendige Setup Values und Keys werden ausschliesslich im zentralen key/value Hash gehalten und von dort in Richtung settings.mrmac serialisiert. Dezentrale Speicher dieser Art sind verboten. Keine überflüssigen File I/O. Es wird nur serialisiert wenn dies notwendig ist und es wird nicht reloaded aus dem Filesystem. Der K/V Hash ist stets inhaltlich gleich zum Inhalt von settings.mrmac.

- Der Bootstrap liest settings.mrmac, verwirft alle unbekannten Keys, schreibt Defaults für Keys die noch nicht im settings.mrmac waren. Die Anwendung von Values erfolg ausschliesslich über die mrmac Stackmachine (alias VM). Wiederholte MRSETUP Statements sind ausdrücklich erlaibt. Anwendung bei Historylisten oder der Speicherung des Workspaces als unsortierte Liste von Window Definitionen.

## Design

- Dialoge müssen 2 Spaces Abstand vom Rahmen zu den Dialogelementen lassen. Alle Radio Button CLuster sind korrekt auszurichten. Label linksbündig über den Clustern.
- Eingabefelder haben Label links mit Text in normaler Colorierung und einem Doppelpunkt. Danach ein Space, dann beginnt das Eingabefeld
- Dialoge müssen automatisch in den Scrollview Modus wechseln wenn das Terminal zu klein wird den Dialog unverzerrt darzustellen
- Data Validatoren dürfen keine Error Dialoge zeigen, sondern müssen Warnings auf der Messageline ausgeben
- Beim Eintritt in Dialoge ist bereits ein Data Validator Lauf über den gesamten Dialog zu führen. Schlägt dieser fehl wird der "Done" Button geghostet
- numerische Eingabefelder sind rechtsbündig darzustellen
- Ranges sind typischerweise als numeric Slider darzustellen
- Buttons
  - Es gibt keine OK Buttons - alle heissen "Done"
  - Buttons auf einer Zeile müssen alle die gleiche Breite haben: Padding
  - Buttons müssen links und rechts ihrer Beschreibung mindestens ein Space bis zu ihrem Rand haben
