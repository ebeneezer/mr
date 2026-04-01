# Kontextwechsel-Dokument (Stand: 2026-04-01)

## 1) Ziel
Dieses Dokument ist die Übergabe für einen neuen Codex-Kontext mit minimalem Informationsverlust.

## 2) Verbindliche Zusammenarbeit (User-Vorgaben)
- Anrede: `Sie` oder `Dr. Raus` (kein Duzen).
- Bei Entscheidungsfragen: immer Optionen mit Begründung + klare Empfehlung.
- Ohne explizite Freigabe keine weiteren Ausbau-Schritte.
- Verwaltung der Versionsnummer des Compilats liegt bei Codex/AI.
- Fokus auf Pragmatik, technische Klarheit, kurze direkte Kommunikation.

## 3) Projektstatus (aktuell)
- Repository: `/home/idoc/mr`
- Branch: `main`
- HEAD: `6d68110`
- Worktree: clean (`git status --short` leer)
- Build: `make mr` grün.
- Probe-Suite: `mr_keyin_probe`, `mr_tofrom_probe`, `mr_tofrom_dispatch_probe`, `mrmac-v1-check` zuletzt grün.

## 4) Technischer Stand (funktional)

### 4.1 MRMAC / VM / Ausführung
- MRMAC ist breit implementiert; TO/FROM-Key-Bindings funktionieren.
- Kein Bytecode-Write ins Filesystem: es werden nur `.mrmac`-Quellen gelesen/geschrieben.
- Reentranzschutz für `KEY_IN`/Hotkey-Dispatch ist drin (Segfault-Loop wurde behoben).

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
- Fokusrahmen: für modale Situationen auf `sfSelected`-Logik korrigiert (Hintergrundfenster wirken inaktiv).
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
2. Optionalen Sync-Pfad nur über `TVISION_AUTO_SYNC=1` nutzen (Mirror/Export/Patch in Vendor-Pfad).
3. Upstream-relevante Änderungen als kleine, klare Patches unter `./patches` halten.
4. Build-Artefakte strikt aus Quell-Diffs heraushalten.
5. Upstream-Abgleich ausschließlich über `./.vendor-cache/tvision.git` und `./build/vendor`.
6. Kein `git subtree`/Submodule einführen.

## 6) Screenshot-Referenzsatz für neuen Kontext (Stilkonstanz)
Diese Bilder sollten im neuen Kontext verfügbar sein, damit UI-Stil/Farb-/Rahmensemantik stabil bleibt.

### A) Pflicht (primäre Stilanker)
1. `/home/idoc/mr/screenshots/screenshot01.png`
2. `/home/idoc/mr/screenshots/screenshot02.png`
3. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_181602.png`
4. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_211450.png`
5. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_211704.png`
6. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_211727.png`
7. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_211906.png`
8. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_211930.png`
9. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_212519.png`
10. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_212543.png`
11. `/home/idoc/mr/documentation/Bildschirmfoto_20260331_215102.png`

### B) Sekundär (historische Referenz / Original-Look-Abgleich)
12. `/home/idoc/mr/documentation/Bildschirmfoto_20260327_161233.png`
13. `/home/idoc/mr/documentation/Bildschirmfoto_20260330_201518.png`
14. `/home/idoc/mr/documentation/Bildschirmfoto_20260328_200731.png`

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
