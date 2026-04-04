# Kontextwechsel-Dokument (Stand: 2026-04-01)

## 1) Ziel
Dieses Dokument ist die Übergabe für einen neuen Codex-Kontext mit minimalem Informationsverlust.

## 2) Verbindliche Regeln (konsolidiert)
- Verbindliche Kontext-/Zusammenarbeitsregeln stehen ausschliesslich in `documentation/Codex Kontext Übergabe.yaml`.
- Dieses Dokument dient nur als historisches Uebergabe-/Statusarchiv.
- Bei Konflikt zwischen Dokumenten gilt immer die YAML-Datei.

## 3) Projektstatus (aktuell)
- Repository: `/home/idoc/mr`
- Branch: `main`
- HEAD: `6d68110`
- Worktree: clean (`git status --short` leer)
- Build: `make mr` grün.
- Probe-Suite: zentral über `make regression-check` und `make mrmac-v1-check` (inkl. `regression/mr-regression-checks --probe ...`) zuletzt grün.

## 4) Technischer Stand (funktional)

### 4.1 MRMAC / VM / Ausführung
- MRMAC ist breit implementiert; TO/FROM-Key-Bindings funktionieren.
- Kein Bytecode-Write ins Filesystem: es werden nur `.mrmac`-Quellen gelesen/geschrieben.
- Reentranzschutz für `KEY_IN`/Hotkey-Dispatch ist drin (Segfault-Loop wurde behoben).
- `MRSETUP` unterstützt jetzt neben `MACROPATH` auch `SETTINGSPATH`, `HELPPATH`, `TEMPDIR`, `SHELLPATH`.

### 4.2 Keystroke Recording
- Alt-F10 Start/Stop aktiv.
- Status-Hinweis in Statuszeile vorhanden.
- Blinkender Recording-Marker im Frame-Markerbereich vorhanden.
- Recording-Glyph: `📼` (Tonband), blinkend.
- Abschlussdialog erfasst Bind-Key via „Press key to bind“ (kein Literal-Tippen nötig).

### 4.3 Macro Manager (MRMAC-zentriert)
- Semantik wie abgestimmt:
  - Kein alter Einzelschritt-Keystroke-Editor.
  - Entweder Recording oder MRMAC-Source-Dateien.
- Dialogfunktionen implementiert:
  - `Create`, `Delete`, `Copy`, `Edit`, `Bind`, `Playback`.
- `Bind<F2>` ist persistent:
  - schreibt/aktualisiert `TO <Key>` in der gewählten `.mrmac`,
  - validiert per Recompile,
  - lädt danach neu.

### 4.4 Hybrid-Bootstrap für gebundene Makros (Option 3 umgesetzt)
- Beim Start:
  - Index der gebundenen Makros (`TO`) aus `.mrmac`-Dateien wird aufgebaut.
- Danach:
  - inkrementelles Warmup (Dateien nach und nach laden/kompilieren).
- Zusätzlich:
  - On-Demand-Fallback beim Hotkey:
    - wenn Binding noch nicht resident ist, wird die passende Datei nachgeladen und erneut dispatcht.

### 4.5 UI-/Darstellungsstand
- Dialog-Kontrast für graue Dialoge angepasst: schwarz auf grau (lesbar).
- Fokusrahmen: zentral in `ui/TMRFrame.cpp` ueber Fokusstatus (`sfFocused`) + Modal-TopView-Gate gesteuert. Doppelrahmen/Window-Controls nur beim fokussierten Objekt; keine dialogspezifischen Sonderpatches.
- Button-Reihenregel fuer Dialoge: `Done/Cancel/Help` konsistent in einer zentrierten Zeile ausrichten (Window-List-Stil), keine versetzte/rechtsdriftende Help-Position.
- UI-Qualitaetsgate verbindlich vor jeder Rueckmeldung: lokaler PTY-Selbsttest der betroffenen Dialoge/Fenster mit Pflichtkriterien
  geometrisch sauber (Ausrichtung/Abstaende/Zentrierung), kein Clipping, korrekte Scrollbars/Farben, Fokusrahmen nur am fokussierten Objekt.
- Pflichtgroessen fuer UI-Selbsttests: mindestens `80x25` sowie aktuelle Problemgrenzen; bei Fehlern zuerst Fix, dann erst weiterer Planfortschritt.
- Null-Toleranz fuer UI-Regressionen: Prioritaet 0; neue Features erst nach Stabilisierung der Darstellung und bestandenen lokalen PTY-Tests aller betroffenen Oberflaechen.
- Benennungsregel: Datei-Felder (Pfad + Dateiname) als `URI` benennen; reine Ordnerfelder als `path`. Datei-URI-Vorbelegung in Dialogen absolut.
- Setup-Layout-Standard: Alle Setup-Dialoge nutzen zentral ein Profilmodell `compact/relaxed` (80x25-first) statt getrennter Dialog-Versionen; nur die Geometrie wechselt profilabhaengig.
- Dialog-Scrollbar-Palette: Scrollbar-Farben in Dialogen werden zentral TVision-weit ueber die globale Dialog-Palette an die Rahmenfarbe angeglichen; keine Einzelbehandlung einzelner Views/Dialoge.
- Setup-Defaults zentralisiert: Bootstrap und Setup verwenden dieselbe Default-Routine zur Ermittlung von Settings-/Macro-/Help-/Temp-/Shell-Pfaden.
- Wenn `settings.mrmac` fehlt, wird sie beim Start automatisch mit den Setup-Defaults erstellt.
- Desktop-Pattern-Diskussion abgeschlossen:
  - Hauptursache für Abweichungen war terminal/font-rendering (v. a. VSCode internal terminal).
  - Externes Terminal ist Referenz für Abnahme.

## 5) Wichtige Architekturentscheidungen (bereits getroffen)
- Coprozessor-/Mailbox-/Staging-Architektur bleibt Leitbild.
- Macro-Lane priorisiert vor Tree-sitter.
- Keystroke-Makro-Workflow:
  - Recording (Alt-F10) + Macro Manager für `.mrmac`.
- Kein Bytecode-Persistenzformat auf Platte.

## 5.1 TVision-Workflow (lokal + upstream-kompatibel)
1. TVision-Änderungen nur unter `./tvision` durchführen und immer mit `make mr` gegen das Gesamtprojekt validieren.
2. Upstream-Remote ist `tvision-upstream` (`https://github.com/magiblot/tvision.git`).
3. Upstream-Aktualisierung über `git subtree` (Prefix `./tvision`) durchführen.
4. Lokale TVision-Abweichungen ausschließlich als kleine, klare Patches unter `./patches` halten.
5. Build-Artefakte strikt aus Quell-Diffs heraushalten.
6. Kein zweiter Vendor-Quellbaum und kein Submodule.
7. Standardbefehl für sicheren Abgleich: `make tvision-sync-safe`.

## 6) Screenshot-Referenzsatz für neuen Kontext (Stilkonstanz)
Diese Bilder sollten im neuen Kontext verfügbar sein, damit UI-Stil/Farb-/Rahmensemantik stabil bleibt.

### A) Pflicht (primäre Stilanker)
1. `/home/idoc/mr/screenshots/screenshot01.png`
2. `/home/idoc/mr/screenshots/screenshot02.png`
3. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_181602.png`
4. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_211450.png`
5. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_211704.png`
6. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_211727.png`
7. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_211906.png`
8. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_211930.png`
9. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_212519.png`
10. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_212543.png`
11. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260331_215102.png`

### B) Sekundär (historische Referenz / Original-Look-Abgleich)
12. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260327_161233.png`
13. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260330_201518.png`
14. `/home/idoc/mr/documentation/pngsjpegs/Bildschirmfoto_20260328_200731.png`

Hinweis: Abnahme für Glyph-/Pattern-Look nicht im VSCode-internen Terminal, sondern im externen Terminal durchführen.

## 7) Nächster Themenblock (für morgen)
Setup-/Konfigurationssystem:
- Vorgabe vom User:
  - keine versteckte JSON-Konfiguration.
  - Konfiguration in `~/.config/mr/settings.mrmac`.
  - neues Token/API z. B. `MRSETUP("MACROPATH", "~/.config/mr")`.

## 8) Empfehlung für das weitere Vorgehen

### Optionen
1. `MRSETUP(...)` als neues MRMAC-Procedure-Token in `settings.mrmac` (whitelist-basiert).
2. Eigener `$SETTING ...;` Header-Block in derselben Datei.
3. Nur globale Variablen ohne neues Setting-Token.

### Empfehlung
Option 1.
- Einheitliche Sprache (MRMAC-only), keine zweite Konfigurationssyntax.
- Saubere Erweiterbarkeit (MACROPATH zuerst, später weitere Keys).
- Klare Sicherheitsgrenze über erlaubte Keys.

## 9) Konkreter Umsetzungsplan (nächste Session)
1. Loader für `~/.config/mr/settings.mrmac` beim Start.
2. `MRSETUP("MACROPATH", "...")` implementieren (zunächst nur dieser Key).
3. Aktuelle Macro-Directory-Auflösung auf den Setting-Wert umstellen.
4. Setup-Dialog anschließend als Editor/Generator für `settings.mrmac` anbinden.

## 10) Protokoll (laufend)
- Das laufende Protokoll ist ab sofort ausgelagert nach `documentation/CHANGELOG_CONTEXT.md`.
- Dieses Dokument ist historisches Archiv; Detailverlauf steht im Changelog und verbindliche Regeln stehen in der YAML-Datei.

## 11) Update 2026-04-01 (spaeter)
- Setup `Edit settings` (Option 1) ist implementiert:
  - Keys: `PAGEBREAK`, `WORDDELIMS`, `DEFAULTEXTS`, `TRUNCSPACES`, `EOFCTRLZ`, `EOFCRLF`,
    `TABEXPAND`, `COLBLOCKMOVE`, `DEFAULTMODE`.
  - Bool-Literale werden in `settings.mrmac` als `true/false` geschrieben.
  - Dialog `Installation and setup -> Edit settings` ist funktional (`Done/Cancel/Help`) und reloadet silent.
- Runtime-Anbindung:
  - `PAGEBREAK` steuert `NEXT_PAGE_BREAK`/`LAST_PAGE_BREAK`.
  - `DEFAULTMODE` wirkt auf neue Fenster und wird nach Settings-Reload auf offene editierbare Fenster angewandt.
  - `DEFAULTEXTS` wird bei Open/Load als Dateiendungs-Fallback genutzt, wenn keine Endung angegeben wurde.
- Verifikation:
  - `make mr` grün.
  - `make regression-check` grün (6/6).
