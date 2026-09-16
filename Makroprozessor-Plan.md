# Konzept: Makroprozessor als Editor

## Zielsetzung
Das langfristige Architekturziel des Projekts besteht darin, die `mrmac`-Stackmaschine nicht nur als angehängte Makrosprache zu betrachten, sondern sie zum zentralen Steuerungselement des Editors zu machen. Der Kompilat-Ausführer soll im Wesentlichen der Editor selbst sein. Dazu muss Logik, die derzeit fest in C++ (insbesondere in den riesigen `if-else`-Ketten in `MRVM.cpp`) verdrahtet ist, durch Kernprimitive abstrahiert und in Makrocode ausgelagert werden.

## Programmtechnische Herangehensweisen (Optionen)

### Option 1: Schrittweises Refactoring zu Dispatch-Tabellen (Tabellensteuerung)
Die Vorgabe `AGENTS.md` verlangt explizit den Abbau von `if`-Ketten zugunsten von Tabellensteuerungen.
- **Ansatz:** Die langen `if-else if`-Ketten für `OP_INTRINSIC` und `OP_PROC` in `VirtualMachine::execute` werden in eine `std::unordered_map` (z. B. Map von String auf Funktionszeiger oder Lambdas) umgewandelt.
- **Vorteil:** Schneller, lokaler Umbau in C++. Erhöht die Wartbarkeit, ohne das Verhalten der Sprache direkt zu verändern. Schafft eine saubere Schnittstelle für die spätere Registrierung neuer Makro-Befehle.
- **Nachteil:** Verändert noch nichts an der konzeptionellen Trennung. Die Logik bleibt in C++ hartkodiert, ist aber besser strukturiert.

### Option 2: Reduktion auf VM-Kernprimitive & Makro-Standardbibliothek
Dies entspricht der tatsächlichen Umsetzung des Paradigmenwechsels.
- **Ansatz:** Die C++-VM behält nur noch echte, atomare Kernprimitive (Speicherzugriff, grundlegende Arithmetik, elementare I/O-Aktionen, Cursor-Bewegungen auf Raw-Ebene). Komplexere Editor-Funktionen werden als `.mrmac`-Makros in einer "Standardbibliothek" umgesetzt, die beim Start des Editors geladen wird.
- **Vorteil:** Erfüllt das Ziel "Makroprozessor als Editor" zu 100 %. Die Editorlogik wird hochgradig programmierbar und flexibel.
- **Nachteil:** Hoher Initialaufwand. Es muss eine Infrastruktur für eine Makro-Standardbibliothek geschaffen werden, inklusive Bootstrapping beim Editor-Start. Performance könnte bei stark genutzten Makros leicht sinken, was aber im Text-Editor-Umfeld meist vernachlässigbar ist.

### Option 3: Hybrid-Ansatz (Iterativer Shift)
Kombination von Option 1 und Option 2 in Phasen.
- **Ansatz:** Zunächst wird die C++-Schicht mittels Dispatch-Tabellen aufgeräumt (Option 1). Anschließend werden nach und nach komplexe Befehle in Makro-Module verschoben und die C++-Implementierungen gestrichen.
- **Vorteil:** Minimiert das Risiko von Regressionen, da jede Funktion einzeln migriert und getestet werden kann. Erfüllt die `AGENTS.md`-Vorgaben zügig.

## Funktionale Bereiche für eine Verlagerung auf die Stackmaschine

Folgende funktionale Bereiche lassen sich gut in die Makroebene verlagern:
1. **Komplexe Text- und Blockoperationen:** Hardcodierte Befehle wie `WORD_WRAP_LINE`, `JUSTIFY_PARAGRAPH`, Block-Einrückung (`INDENT_BLOCK`, `UNDENT_BLOCK`) oder komplexere Cursor-Bewegungen (`MOVE_WIN_TO_NEXT_DESKTOP`).
2. **UI-Handling und Dialoge:** Der Aufbau von Menüs und Dialogen (aktuell `UI_DIALOG`, `UI_LABEL` etc. als C++-Backend) könnte stärker als Makro abgebildet werden, das lediglich ein Kernprimitiv "Render Screen/Dialog" aufruft.
3. **Konfigurationsparsing (`MRSETUP`):** Das Auswerten von Settings-Strings passiert aktuell im C++-Bootstrapper. Dies könnte ein Makro übernehmen, welches elementare Setzer für Kern-Settings aufruft.
4. **Erweiterte Suchen und Ersetzen:** Das Suchen und Ersetzen über mehrere Dateien (`MULTI_FILE_SEARCH_REPLACE`) ließe sich als Schleife auf Makroebene realisieren, die lediglich elementare Suchprimitive für einzelne Dateien aufruft.

## Begründete Empfehlung

**Empfehlung: Option 3 (Hybrid-Ansatz)**

*Begründung:*
Ein sofortiger Komplettumbau (Option 2) ist zu riskant und würde große Teile der Codebasis destabilisieren (insbesondere in Hinblick auf Regressionstests und Verträge in `documentation/architecture/`).

Als ersten und unmittelbaren Schritt empfehle ich die **Einführung einer Tabellensteuerung in `mrmac/MRVM.cpp` (entsprechend Option 1)**.
- Das bricht die gewaltigen `if-else`-Ketten bei `OP_PROC` und `OP_INTRINSIC` auf.
- Es erfüllt direkt die Regel aus `AGENTS.md` (Verbot von langen if-Ketten).
- Es schafft die technische Grundlage (eine saubere Dispatch-Architektur), um später einzelne Kommandos problemlos in Makro-Primitive umzuwandeln oder ganz zu entfernen, wenn die Standardbibliothek übernimmt.

Sobald die Dispatch-Tabellen implementiert und durch die Regression-Suite validiert sind, kann in einem zweiten Schritt beispielhaft eine funktionale Domäne (z.B. komplexe Blockoperationen) komplett in eine `mrmac`-Standardbibliothek ausgelagert werden.
