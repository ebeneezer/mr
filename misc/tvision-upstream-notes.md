# TVision Upstream Notes

Stand: 2026-03-29

## Ziel

Diese Notiz trennt sauber zwischen:

- Anwendungsfehlern in `mr`
- echten, generischen TVision-Themen
- lokalem Vendoring-Aufwand beim Einsatz von `magiblot/tvision`

Referenz-Upstream:

- <https://github.com/magiblot/tvision>

## Wichtigster Befund

Der reproduzierte Segfault beim Pfad `File -> Load...` war **nicht** durch einen Linux-Dateisystemfehler in TVision verursacht.

Die eigentliche Ursache war ein Buffer-Overflow in `TCommandSet`, ausgelöst durch zu hohe, projektspezifische Command-IDs in `mr`.

Relevante Stellen:

- `mr.cpp`: eigene Commands starteten bei `1000`
- `ui/mrwindowlist.cpp`: eigene Commands starteten bei `3200`
- `tvision/include/tvision/views.h`: `uchar cmds[32]`
- `tvision/source/tvision/tcmdset.cpp`: Zugriff via `cmds[loc(cmd)]`

Konsequenz:

- TVision lässt aktuell Command-IDs außerhalb des 256-Bit-Bereichs ungeschützt zu.
- Das ist eine **Robustheitslücke in TVision**.
- Der konkrete Auslöser lag aber in `mr`, nicht in TVision.

## Verifikation gegen aktuellen Upstream

Am 2026-03-29 wurde der relevante TVision-Stand noch einmal direkt gegen den aktuellen `master` von `magiblot/tvision` geprüft.

Ergebnis:

- `include/tvision/internal/findfrst.h` entspricht dem aktuellen Upstream
- `source/platform/findfrst.cpp` entspricht dem aktuellen Upstream
- `include/tvision/internal/codepage.h` weicht lokal weiterhin vom aktuellen Upstream ab
- `include/tvision/views.h` und `source/tvision/tcmdset.cpp` haben im aktuellen Upstream weiterhin keinen Range-Guard für `TCommandSet`

Damit ist wichtig:

- der lokale Git-Diff unter `tvision/` ist **nicht automatisch** identisch mit einer echten Abweichung zum aktuellen magiblot-Upstream
- `findfrst.*` ist derzeit **keine** echte lokale Fork-Abweichung mehr

## Aktuelle echte TVision-Quelltextabweichungen

### Gegen den lokalen Projekt-Git-Stand

Im Workspace liegen unter `tvision/` als Source-Diffs im lokalen Repository:

1. `tvision/include/tvision/internal/codepage.h`
2. `tvision/include/tvision/internal/findfrst.h`
3. `tvision/source/platform/findfrst.cpp`

### Gegen den aktuellen Upstream `magiblot/tvision`

Bestätigte echte Abweichung im Source-Baum:

1. `tvision/include/tvision/internal/codepage.h`

Zusätzlich existiert ein **nicht lokal angewendeter**, aber sinnvoller Upstream-Kandidat:

2. defensiver Guard für `TCommandSet` in `include/tvision/views.h` und `source/tvision/tcmdset.cpp`

Der große Rest unter `tvision/build/`, `tvision/build-asan/` oder `tvision/build-debug/` sind Build-Artefakte und dürfen **nicht** als Upstream-Patchmenge betrachtet werden.

## Was sich sinnvoll upstreamen lässt

### 1. Defensiver Guard in `TCommandSet`

Empfehlung: upstreamen.

Begründung:

- verhindert Speicherkorruption bei fehlerhaften Command-IDs
- ändert keine öffentliche API
- ist plattformunabhängig
- hilft auch anderen Anwendungen

Der Vorschlag liegt als Patch-Datei vor:

- `patches/0001-tvision-guard-command-range.patch`

### 2. Header-Fix in `codepage.h`

Empfehlung: upstreamen.

Begründung:

- `codepage.h` verwendet `TStringView`
- der Include auf `<tvision/tv.h>` macht die Abhängigkeit explizit
- sehr kleiner, generischer Compile-Fix

Der Vorschlag liegt als Patch-Datei vor:

- `patches/0002-tvision-codepage-include-tv-h.patch`

## Was derzeit nicht belastbar upstream-fähig ist

### `findfrst.*`

`findfrst.h` und `findfrst.cpp` sind im aktuellen magiblot-Upstream bereits auf dem Stand, der lokal vorliegt.

Deshalb gilt:

- kein weiterer lokaler Fork-Druck an dieser Stelle
- kein pauschaler Linux-Bugreport zu `findfrst.*` aus dem beobachteten `mr`-Segfault ableiten
- nur dann erneut aufgreifen, wenn ein eigener, separater Testfall für `findfrst.*` vorliegt

## Warum Vererbung allein nicht reicht

Vererbung hilft bei:

- eigenem Fensterverhalten
- Editor-Policy
- Anwendungskommandos
- UI-Anpassungen

Vererbung hilft **nicht** bei:

- `TCommandSet`
- internen Header-Abhängigkeiten
- `FindFirstRec`
- anderen internen TVision-Implementierungsdetails

Für solche Fälle braucht es kleine, explizite Vendor-Patches.

## Empfohlene Integrationsstrategie

### Möglichkeit 1: TVision weiter als vendorten Quellbaum pflegen

Begründung:

- kleinster Umbau
- passt zum aktuellen Repository
- schnell praktikabel

Nachteile:

- Upstream-Synchronisation bleibt manuell
- Build-Artefakte verschleiern echte Diffs

### Möglichkeit 2: TVision als `git subtree` führen

Begründung:

- guter Mittelweg zwischen Vendoring und Upstream-Nähe
- Upstream-Importe bleiben nachvollziehbar
- keine zusätzliche Submodule-Komplexität

Nachteile:

- einmaliger Umbau nötig
- Team muss den Workflow konsequent einhalten

### Möglichkeit 3: TVision als Submodul führen

Begründung:

- Upstream-Trennung sehr klar
- lokale Patches können sauber auf eigener Branch-Serie liegen

Nachteile:

- mehr Bedienaufwand
- für Einzelrepo-Arbeit oft unnötig unbequem

## Empfehlung

Empfohlen ist **Möglichkeit 2: `git subtree`**, sobald die aktuelle Stabilisierung abgeschlossen ist.

Bis dahin pragmatisch:

1. nur echte Source-Patches unter `tvision/include` und `tvision/source` zulassen
2. Build-Artefakte bei jeder Upstream-Betrachtung ignorieren
3. lokale TVision-Änderungen zusätzlich als Patch-Queue unter `patches/` ablegen
4. Upstream-fähige Änderungen klein und thematisch getrennt halten

## Was ich aktuell direkt tun kann

Ich kann derzeit keinen PR direkt bei magiblot einreichen, weil in der vorhandenen GitHub-Anbindung nur Schreibrechte auf `ebeneezer/*` sichtbar sind, nicht auf `magiblot/tvision`.

Ich kann aber:

1. Upstream-taugliche Patches vorbereiten
2. Issue-/PR-Texte ausformulieren
3. den lokalen TVision-Diff in einen sauberen Patch-Stapel überführen
