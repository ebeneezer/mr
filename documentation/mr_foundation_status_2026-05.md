# MR Foundation Status 2026-05

## Zweck

Dieses Dokument hält den nach der großen Bereinigungs- und Reaktivierungsphase erreichten Stand fest.
Es dient als saubere Übergabegrundlage für einen neuen Arbeitskontext.

Der Fokus dieser Tranche lag auf:

- Stabilisierung von PieceTable/AddBuffer
- Wiederherstellung eines belastbaren Editor-Hotpaths
- Reaktivierung von Syntax, Folding und MiniMap auf bereinigter Basis

## Erledigt

### Dokumentkern

- der editierte Dokumentzustand verwendet wieder einen belastbaren exakten Zeilenindex
- Whole-buffer-Hotpaths wurden aus kritischen Edit-Pfaden entfernt
- Insert- und Erase-Pfade wurden so bereinigt, dass große Dateien bedienbar bleiben
- Block-Delete läuft nicht mehr über Vollsnapshot plus Whole-buffer-Replace
- Undo-/Redo- und Restore-Pfade wurden so entlastet, dass sie den Editor nicht mehr blockierend destabilisieren

### Quit- und Lebensdauerpfade

- `exit-discard` blockiert nicht mehr über teure Nachläufe wie `longestLineWidth()`
- Dirty-Gating funktioniert wieder korrekt
- der verbleibende Quit-Pfad ist im normalen Lauf wieder praktisch sofort

### Syntax-Colorierung

- Syntax-Colorierung ist wieder aktiv
- die Colorierung läuft viewportnah und bedienbar auch auf sehr großen Dateien
- der UI-Thread rechnet dabei keine großen Vollscans
- aufgestaute Paging-Ereignisse wurden durch Coalescing entschärft

### Folding

- Folding ist wieder aktiv
- der Fold-Pfad nutzt einen viewportzentrierten Up/Down-Scanner
- der Scanner arbeitet dateigrößenunabhängig
- die Last verteilt sich sauber und fällt sofort ab, sobald der Viewport ruht
- Fold-Overlay-/Task-Anzeige wurde so bereinigt, dass kein falscher Dauerzustand hängen bleibt

### MiniMap

- die MiniMap ist wieder sichtbar und funktional
- die MiniMap nutzt denselben gemeinsamen viewportzentrierten Scannerkern wie Folding
- die MiniMap ist von der früheren globalen `pieceTableOnly`-Blockade entkoppelt
- verspätete, aber noch brauchbare Warmup-Ergebnisse dürfen sichtbar projiziert werden, statt nur eine leere Fläche zu zeigen

## Aktueller Qualitätsstand

- Inserts: sauber und auch in großer Menge bedienbar
- normale Deletes: sauber
- Block-Delete: sofort
- `PageUp` / `PageDown`: bedienbar, kein langer Event-Stau nach Loslassen
- Syntax: bedienbar auf sehr großen Dateien
- Folding: schnell genug und mit sauberem CPU-Abklingen
- MiniMap: funktional korrekt und sichtbar
- `exit-discard`: praktisch sofort

## Noch offen

### 1. MiniMap-Reaktivität

Die MiniMap läuft auf der neuen gemeinsamen Foundation noch spürbar nach.

Wichtig:

- das ist kein Stabilitäts- oder Korrektheitsfehler mehr
- die Foundation soll dafür nicht wieder auf einen alten Sonderpfad zurückgebogen werden
- der nächste Zug sollte reiner Reaktivitäts-Feinschliff auf der bestehenden Architektur sein

Ziel:

- frühere sichtbare Projektion des letzten brauchbaren Ergebnisses
- aggressiveres, aber weiterhin sauberes viewportnahes Scheduling
- kein Vollscan im UI-Thread
- kein Rückbau des gemeinsamen Scannerkerns

### 2. Bestandskonsolidierung außerhalb des akuten Hotpaths

Der Editor ist jetzt wieder auf einer sauberen Basis. Offen bleibt nur noch reguläre Nacharbeit am Bestand:

- weitere Dokumentation der erreichten Architektur
- allgemeine Konsistenzpflege zwischen Editor, Derived State und Dialogpfaden
- eventuelle spätere Komfortarbeit an Paging- oder MiniMap-Reaktivität

### 3. Spätere Spracharbeit nur bei Bedarf

Der nächste mögliche Sprachzug wäre `fish`, falls er ausdrücklich gewünscht wird.

Das ist aber nicht Teil der jetzt abgeschlossenen Fundament-Tranche.

## Nicht als offener Kernfehler zu behandeln

Diese Punkte gelten mit dem aktuellen Stand nicht mehr als fundamentale Baustellen:

- unbedienbare Insert-Latenzen
- blockierende Whole-buffer-Deletes
- Quit-Latenz durch `longestLineWidth()`
- Syntax-bedingte UI-Unbedienbarkeit
- Folding-Last, die nach Viewport-Ruhe weiterläuft
- schwarze/leere MiniMap durch den früheren deaktivierten MiniMap-Pfad

## Empfehlung für den nächsten Kontext

Der nächste Kontext sollte nicht wieder breit beginnen, sondern mit genau einer kleinen Komfort-Tranche:

1. MiniMap-Reaktivitäts-Feinschliff auf der bestehenden Foundation
2. kein Rückbau auf Sonderpfade
3. keine neue Fundament-Arbeit ohne neuen harten Befund
