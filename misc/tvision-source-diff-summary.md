# TVision Source Diff Summary

Stand: 2026-03-29

## Zweck

Diese Datei trennt:

- echte Source-Diffs
- Build-Artefakte
- lokale Änderungen gegen Projektbasis
- echte Änderungen gegen den aktuellen Upstream `magiblot/tvision`

## A. Lokaler Git-Diff unter `tvision/`

Im lokalen Repository erscheinen derzeit viele Änderungen unter `tvision/`.

Fast alle davon sind:

- `tvision/build/**`
- `tvision/build-asan/**`
- `tvision/build-debug/**`
- generierte CMake-Dateien
- Objektdateien
- Binärartefakte

Diese Dateien sind **kein** sinnvoller Input für Upstream-Arbeit.

## B. Relevante Source-Dateien im lokalen Projekt-Diff

Die echten Source-Dateien im lokalen Projekt-Diff sind:

1. `tvision/include/tvision/internal/codepage.h`
2. `tvision/include/tvision/internal/findfrst.h`
3. `tvision/source/platform/findfrst.cpp`

## C. Verifikation gegen aktuellen magiblot-Upstream

Direkt gegen `magiblot/tvision` geprüft:

- `findfrst.h`: lokal entspricht Upstream
- `findfrst.cpp`: lokal entspricht Upstream
- `codepage.h`: lokal weicht vom Upstream ab
- `views.h`/`tcmdset.cpp`: Upstream hat weiterhin keinen Schutz gegen out-of-range-Command-IDs

## D. Aktuell bestätigte echte lokale Upstream-Abweichung

### 1. `codepage.h`

Lokale Änderung:

- zusätzlicher Include auf `<tvision/tv.h>`

Bewertung:

- klein
- generisch
- upstream-fähig

## E. Nicht lokal angewendet, aber als Upstream-Patch vorbereitet

### 2. `TCommandSet`-Guard

Betroffene Upstream-Dateien:

- `include/tvision/views.h`
- `source/tvision/tcmdset.cpp`

Bewertung:

- echter Robustheitsgewinn
- unabhängig von `mr`
- sollte als eigener kleiner Upstream-Patch behandelt werden

## F. Praktische Regel für die weitere Arbeit

Für TVision-Synchronisation künftig nur diese Bereiche betrachten:

- `tvision/include/**`
- `tvision/source/**`
- ggf. `tvision/CMakeLists.txt`
- ggf. `tvision/source/CMakeLists.txt`

Diese Bereiche bei Upstream-Fragen grundsätzlich ignorieren:

- `tvision/build/**`
- `tvision/build-asan/**`
- `tvision/build-debug/**`
- alle generierten Binär- und Objektdateien
