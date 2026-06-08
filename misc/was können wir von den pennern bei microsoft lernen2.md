# Was koennen wir von den Pennern bei Microsoft lernen?

Diese Notiz spiegelt unsere Architektur gegen die Eigenschaften, die VS Code trotz Electron stabil und schnell machen. Der Punkt ist nicht, Electron zu kopieren, sondern die disziplinierenden Prinzipien auf `mr` zu uebertragen.

## Was bereits gut passt      trallal�

- Deblocking und Background Processing sind von Beginn an Teil der Architektur: Coprocessor, deferred UI, Derived State, Syntax-/Minimap-Ableitungen und externe Kommandos blockieren die UI nicht absichtlich.
- Fachliche Isolation ist vorhanden: Compiler-Diagnostik ist nicht direkt Editor-Malcode, sondern Build Output, Parser, Problems Pane, Source-Jump und SideKick-Projektion.
- Viewport-Denken ist etabliert: Minimap, Scrollbars, SideKick-Sichtbarkeit und Bento-Panes arbeiten zunehmend aus Sicht des sichtbaren Ausschnitts.
- UI-Foundation statt Feature-Sonderwege: BentoBox kann als gemeinsamer Traeger fuer Output, Problems, Debugging, SideKick und weitere Werkzeug-Panes dienen.
- Textmodell als zentrale Wahrheit ist angelegt: Piece Table und Editor-State liefern die Datenbasis, UI-Komponenten sollten nur Projektionen daraus sein.

## Wo wir von VS Code lernen koennen
                                          



- Aktivierungsbudgets pro Subsystem einfuehren: unterscheiden, was vor First Paint, vor First Edit, vor First Command oder erst bei echter Nutzung geladen werden muss.
- Bootstrap-Messung dauerhaft behalten und nur wenige, klare Phasen loggen: total, settings load/apply, first editor open, first paint, first syntax ready, first minimap ready und command latency fuer Build/Search/Open.
- Dirty-Region- und Repaint-Politik schaerfen: Pane-Chrome nur bei Layout/Fokus/Statuswechsel, Scrollbars nur bei Range-/Position-/Visibility-Wechsel, Minimap nur bei Document-/Derived-State-Wechsel, SideKick nur bei Position/Text/Visibility-Wechsel.
- Cancellation und Epochs konsequenter verwenden: alte Analyseergebnisse duerfen billig verworfen werden, wenn Dokument oder View-State nicht mehr zum Snapshot passen.
- Extension-Host-Prinzip intern anwenden: Fachfeatures duerfen UI-Invarianten nicht direkt verletzen und muessen ueber klare Projektionen an die Oberflaeche gehen.
- Projektionen konsequent als Projektionen behandeln: Diagnostics, Problems, SideKick, Minimap und Scrollbars besitzen keine eigene fachliche Wahrheit, sondern zeigen abgeleiteten Zustand.

## Kanban-Karte: Zentrale Dirty-Projection-Optimierung

### Titel

Centralized dirty projection for UI pump

### Ziel

Die UI-Pumpe soll nicht mehr nach jedem relevanten Event vorsorglich breite Draw-Pfade ausloesen. Stattdessen markieren Views gezielt, welche Projektion ungueltig ist: content, chrome, scrollbar, layout oder overlay. Die UI-Pumpe loest diese Dirty-Bits deterministisch in einer festen Reihenfolge auf.

### Problem

BentoBox hat gezeigt, dass Source, Tool-Panes, Scrollbars, Minimap, SideKick und Fokus-Chrome leicht redundante Draws und Z-Order-Risiken erzeugen. Einzelne Fixes stabilisieren Symptome, aber die zentrale Frage bleibt: Wer entscheidet, wann welche Projektion wirklich neu gezeichnet werden muss?

### Vorschlag

- Kleinen zentralen Dirty-State einfuehren, ohne eine grosse UI-Basisklasse fuer fachliche Details zu bauen.
- Dirty-Bits verwenden: content, chrome, scrollbar, layout, overlay.
- Layout-Dirty immer vor Content/Chrome/Scrollbars aufloesen.
- Draw-Pfade nur ausfuehren, wenn ihr Dirty-Bit gesetzt ist oder der relevante Vergleichszustand geaendert wurde.
- Dirty-Bits erst nach erfolgreichem Zeichnen konsumieren.
- Pilot zuerst an BentoBox/Pane-Chrome/Scrollbars, nicht sofort an allen Views.

### Nicht-Ziele

- Keine zweite Eventloop.
- Kein asynchrones Zeichnen.
- Keine neue Ownership von TVision-Views.
- Keine grosse `MRRenderablePaneBase`, die Layout, Scrollbars, Chrome, Fokus, Minimap und Content fachlich vermischt.
- Kein opportunistischer Umbau der gesamten Editor-Hierarchie.

### Geschuetzte Architektur

Ja, bei Umsetzung. Die Karte beruehrt TVision Drawing/Event Mechanics und damit den TVision-Integrationsvertrag. Vor Implementierung ist ein expliziter Plan erforderlich.

### Invarianten

- TVision bleibt Besitzer der Event- und Draw-Sequenz.
- Keine `show()`/`hide()`-Seiteneffekte waehrend Destroy/Close.
- Views zeichnen nur ihre eigene Projektion.
- Dirty-State beschreibt UI-Ungueltigkeit, nicht fachliche Wahrheit.
- Derived State und Compiler-Diagnostik bleiben Snapshot-/Epoch-gebunden.                 trullala

### Akzeptanzkriterien

- BentoBox zeichnet Pane-Chrome, Source-Content, Tool-Panes, Scrollbars und SideKick nur bei relevanter Aenderung.
- Keine Regression bei Fokuswechsel, Pane-Split, Close, Maximize, SideKick-Tracking und Mousewheel.
- Scrollbars bleiben korrekt sichtbar/versteckt und reclaimen Platz wie bisher.
- Close-Pfade bleiben crash-freix.
- Build und Regression laufen gruene durch.

### Pruefplan

- `make clean all CXX=clang++`
- `make regression-check CXX=clang++`
- Manueller Smoke-Test: Bento splitten, Build ausfuehren, Problems klicken, SideKick scrollen, Pane close/maximize, Source close, Mousewheel in Source und Tool-Panes.





## Kanban-Karte: File Compare Orientierung

### Titel

File Compare semantic navigation and overview

### Erledigt

- Change-Zaehler je Pane mit sichtbarer Change-Range und sichtbaren/totalen Delete-/Insert-Zeilen.
- FC-Minimap-Farben und FC-spezifische Diff-Projektion.
- Hunk-Navigation per Next/Previous Difference.

### Offen

- Kompakte Diff-Uebersicht als eigene Hunk-Liste oder Side-Pane.
- Sync-Anker sichtbarer machen.
- Kleiner Statusbereich pro Diff-Hunk.

### Invarianten

- Panes bleiben synchron gekoppelt.
- Diff-Daten bleiben aus `fileCompareHunks`/`fileCompareChangeGroups` abgeleitet.
- Keine neue Render-Seitenroute neben TVision.

>
