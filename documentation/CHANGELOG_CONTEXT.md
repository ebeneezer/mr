# CHANGELOG_CONTEXT

Laufendes Projektprotokoll fuer Kontextwechsel und schnelle Wiederaufnahme.

## Warmup-Regel
- Vor jeder neuen Session zuerst diese Datei lesen.
- Danach `Codex Kontext Übergabe.yaml` und `KONTEXTWECHSEL_2026-04-01.md` als Struktur-/Policy-Quelle nutzen.

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
- Fokus-/Rahmenregel erneut fixiert:
  - Doppelrahmen und Fenster-Controls richten sich strikt nach `sfSelected`.
  - `sfActive`/`sfFocused` werden fuer diese Darstellungsentscheidung nicht mehr als Fallback genutzt.
