# MRMAC v1 Status (2026-03-31)

## Ziel
MRMAC v1 wird als stabiler Sprach- und Laufzeitstand eingefroren, mit reproduzierbarer lokaler Verifikation.

## Definition of Done
- `make mr` ist erfolgreich.
- `make stage-profile-probe` ist erfolgreich.
- Alle v1-staged Referenzmakros sind für staged Background-Ausführung zulässig:
  - `canStage=1`
  - `unsupported=<none>`
- Navigations-/Markierungs-Probes laufen ohne Fehler:
  - `regression/mr-regression-checks --probe staged-nav`
  - `regression/mr-regression-checks --probe staged-mark-page`
- Ein vollständiger Test-Makro-Compile-Sweep über `mrmac/macros/test*.mrmac` ist fehlerfrei.

## V1-Abdeckung (staged Snapshot-basiert)
- Editor-Grundzustand: Cursor/Zeile/EOF/EOL, Zeilenzugriff, Insert-/Indent-Optionen.
- Text- und Selektionsoperationen: `TEXT`, `PUT_LINE`, `CR`, `DEL_*`, `REPLACE`.
- Navigation/Marken/Seiten: `LEFT/RIGHT/UP/DOWN`, Wortnavigation, `PAGE_*`, `MARK_*`.
- Blockzustand: Blockmodus, Blockgrenzen, Marking.
- Dateistatus: `FIRST_SAVE`, `EOF_IN_MEM`, `BUFFER_ID`, `TMP_FILE`, `TMP_FILE_NAME`, `FILE_NAME`, `FILE_CHANGED`.
- Fenster-/Linkstatus: `CUR_WINDOW`, `LINK_STAT`, `WINDOW_COUNT`, `WIN_X1..WIN_Y2`.
- Deferred UI-Kommandos (staged, apply-on-commit): `CREATE_WINDOW`, `DELETE_WINDOW`,
  `MODIFY_WINDOW`, `LINK_WINDOW`, `UNLINK_WINDOW`, `ZOOM`, `REDRAW`, `NEW_SCREEN`,
  `SWITCH_WINDOW`, `SIZE_WINDOW`.
- Runtime-Optionen und Suchzustand: `IGNORE_CASE`, `TAB_EXPAND`, Last-Search Snapshot.
- Globale Variablen: `GLOBAL_*`, `SET_GLOBAL_*`, `FIRST_GLOBAL`, `NEXT_GLOBAL`.
- Makro-Registry-Lesezugriffe: `INQ_MACRO`, `FIRST_MACRO`, `NEXT_MACRO`.
- `RUN_MACRO` in staged Pfaden (inkl. Session-State-Propagation).
- Externe I/O-Befehle und Dateisystem-Effekte:
  - `LOAD_FILE`, `SAVE_FILE`, `SAVE_BLOCK`, `LOAD_MACRO_FILE`, `CHANGE_DIR`, `DEL_FILE`
  - `FILE_EXISTS`, `FIRST_FILE`, `NEXT_FILE`, `GET_ENVIRONMENT`
- Harte UI-Operationen (Fenster-/Screen-Management), die nicht als staged Commit modelliert sind:
  - `CREATE_WINDOW`, `DELETE_WINDOW`, `MODIFY_WINDOW`, `LINK_WINDOW`, `UNLINK_WINDOW`
  - `ZOOM`, `REDRAW`, `NEW_SCREEN`, `SWITCH_WINDOW`, `SIZE_WINDOW`
  - TVision-Dialogaufrufe (`MessageBox`)
- TVISION Elemente müssen über TVCALL aufrufbar sein.
- es muss eine umfassende Referenz der mrmac Sprache in englischer Sprache erstellt werden mit Beispielen. Vorlage kann das Referenzhandbuch des Originals in documentation sein.
## Reproduzierbare Verifikation
Empfohlener Gesamtcheck:

```bash
make mrmac-v1-check
```

Direkter Aufruf des Suite-Skripts:

```bash
./misc/run_mrmac_v1_suite.sh
```
