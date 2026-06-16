# LSP V2 Context Handoff

Stand: 2026-06-16

## Aktueller Branch

- Branch: `feature/lsp-v2-external-service`
- Remote-Stand: synchron mit `origin/feature/lsp-v2-external-service`
- Lokaler Reststatus: `misc/testsnippet.c` ist durch manuelle Tests geaendert und soll nicht automatisch zurueckgesetzt oder committed werden.

## Arbeitsmodus und Stilvorgaben

- Den Maintainer formal ansprechen.
- Antworten, Plaene, Reviews und Commit-Messages auf Deutsch.
- Code, Identifier und Kommentare auf Englisch.
- C++20 ist technischer Standard, aber Stilziel ist klassisches, gut lesbares C++18.
- Keine atomaren Wrapper, keine sekundaeren Registries, keine offenen Kapselungen.
- Tabellensteuerung ist gegenueber langen `if`-Ketten bevorzugt.
- Im LSP-Umgriff keine neue Registry neben dem zentralen K/V Store; LSP-Konfiguration gehoert unter den Toplevel-Schluessel `LSP`.
- Keine opportunistischen Eingriffe in Settings, VM, Keymap, TVision-Mechanik oder sonstige geschuetzte Architektur.

## LSP V2 Architekturpfad

Die zentrale Erkenntnis aus V1 war: MR braucht keine verstreute LSP-Speziallogik, sondern eine kontrollierte External-Service-Schicht.

Kernprinzipien:

- MR bleibt Autoritaet ueber Editorzustand, Dokumentversion und Workspace.
- Der LSP-Server besitzt nur einen Protokollspiegel.
- MR synchronisiert geoeffnete Dokumente mit Versionen und prueft eingehende Ergebnisse gegen Request-ID und Dokumentversion.
- Der MR-Workspace ist der fachliche Workspace. Sichtbare Steuerdateien sollen nicht erzeugt oder verlangt werden.
- Git darf natuerlich `.git` verwenden; MR soll aber nicht zur folder-hoerigen Bedienoberflaeche werden.
- LSP-Dienste sollen als externe Services ueber eine gemeinsame Schicht angebunden werden.

Aktuell genutzte oder begonnene Dienste:

- Diagnostics
- Completion
- Hover
- Definition
- References
- Code Actions als Struktur vorbereitet, aber nur nutzbare Actions duerfen angezeigt werden

## Wichtige Implementierungsstuecke

Relevante Bereiche:

- `lsp/`
- `app/services/MRLspAppService.*`
- `app/services/MRLspServiceSession.*`
- `app/services/MRServiceResults.*`
- `app/services/MRLspEditorSource.*`
- `app/services/MRLspServerProfile.*`
- `app/MRCommandRouter.cpp`
- LSP-Probes in `regression/`

Der LSP Protocol Shaper ist kein fachlicher Language Server, sondern ein Testwerkzeug fuer Transport, Lifecycle, Request-Zuordnung, Diagnose-Interleaving und kontrollierte Antwortformen.

## Verifizierter letzter Fix

Problem:

- Beim manuellen Test wurde `for` in den Editor geschrieben und direkt danach LSP Completion ausgeloest.
- Der erste Completion-Aufruf lieferte leer oder wurde scheinbar von Diagnostics ueberholt.
- Der Maintainer bestaetigte, dass der manuelle Ablauf nicht als Batch-Ersatz dienen soll; der Test musste automatisiert nachvollzogen werden.

Selbsttest-Befund:

- Direkt gegen `clangd` reproduziert:
  - `didOpen` mit Buffer inklusive `for`
  - sofortige Completion: `0` Items
  - nach kurzer Wartezeit: Completion enthaelt `for`-Kandidaten
- Schwellenbeobachtung:
  - `0 ms` bis `50 ms`: leer
  - ab ca. `100 ms`: erste brauchbare Items
  - bis ca. `300 ms` vollere Ergebnisliste

Fix:

- `requestLspCompletionCommand()` ignoriert eine erste leere Completion-Antwort genau einmal.
- Vor dem Retry wird kurz weiter gepollt.
- Der Retry verwendet eine neue Request-ID.
- Erfolgreiche Completion-Antworten werden nicht verzoegert.

Letzter Commit:

- `11b10152 Verzoegere leeren LSP Completion Retry`

Maintainer-Sichttest:

- Fix wurde bestaetigt.

## Letzte relevante Commit-Kette

```text
11b10152 Verzoegere leeren LSP Completion Retry
62268085 Protokolliere rohe leere LSP Completion Antwort
a7883331 Protokolliere LSP Completion Cursorzeile
d444f894 Wiederhole leere LSP Completion Antwort
9ea042b0 Schuetze LSP Completion Command Priorisierung
98466174 Protokolliere LSP Completion Timeline
8a5a44d2 Schuetze LSP Completion Insert Text
2a0b3fc2 Fixiere direkten LSP Completion Report
```

## Letzte Verifikation

Vor dem Handoff erfolgreich ausgefuehrt:

```sh
make regression-check lsp-service-integration-probe CXX=clang++
./regression/mr_lsp_service_integration_probe
make clean all CXX=clang++
```

Ergebnis:

- Regression: `82 passed, 0 failed`
- LSP Service Integration Probe: bestanden
- Clean-Build: Exitcode `0`
- Keine Compilerwarnungen im Build-Log gefunden

## Manuell bestaetigte LSP-Funktionalitaet

Vom Maintainer bestaetigt:

- Hover funktioniert.
- Completion funktioniert nach Fix.
- Completion fuegt den Insert-Text ein, nicht die ganze Signatur.
- Definition-Sprung funktioniert.
- References funktioniert.
- Results-Dialog zeigt nur nutzbare Action-Zeilen; unbrauchbare Code-Action-Platzhalter wurden entfernt.

## Bekannte Entscheidungen und Stolperstellen

- Clangd akzeptiert nicht jedes GCC-gueltige C-Konstrukt gleich; Tests duerfen nicht blind von GCC-Semantik ausgehen.
- Diagnostics koennen zwischen Request und Antwort eintreffen. Das darf Completion/Definition/References nicht stoeren.
- Zuordnung muss ueber Request-ID und Dokumentversion erfolgen, nicht ueber Reihenfolge im globalen Ergebnisstrom.
- Leere Completion-Antworten koennen echte Server-Timing-Effekte sein, nicht zwingend Parserfehler in MR.
- Nicht nutzbare Code Actions duerfen nicht im UI angeboten werden.
- UI-Dialoge sind derzeit noch eher Pruefoberflaechen; langfristig soll LSP staerker in die zentrale Kontext-/Angebotsidee eingebunden werden.

## Naechster sinnvoller Schritt

Nach dem bestaetigten Completion-Fix sollte der LSP-Zug wieder vom Debugging in den geplanten Ausbau wechseln:

1. Log-/Text-basierten LSP-Testpfad weiter ausbauen, damit MR selbst definierte LSP-Szenarien fahren kann.
2. Completion/Diagnostics/Definition/References/Hover mit Protocol-Shaper- und echten `clangd`-Faellen regressionssicher machen.
3. Danach erst UI-Integration vertiefen.
4. Bei UI-Arbeiten besonders beachten:
   - Kontextmenue/Angebotslogik ist zentrale Designidee.
   - MR soll nicht "fragen", sondern an Cursor- oder Mausposition anbieten, welche Dienste dort abrufbar sind.
   - Hover bleibt Ziel im Endausbau.

## Offene Arbeitsbaumwarnung

`misc/testsnippet.c` ist absichtlich nicht bewertet. Diese Datei wurde fuer manuelle Sichttests benutzt und darf nicht ohne Maintainerauftrag zurueckgesetzt oder committed werden.
