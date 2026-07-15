# Hex Editor – Neustartplan

## Status und Ausgangsbasis

Dieser Plan ersetzt den verworfenen Ausbau auf Branch `hexeditor`. Ausgangsbasis
ist `origin/main` bei `b47de8ed`.

Der erste Versuch ist nicht fortzuführen. Insbesondere sind zu entfernen:

- Hex-spezifische Zweige in `MRBentoBox`,
- eine Sonderbehandlung des Primär-Leafs außerhalb der Pane-Hierarchie,
- nachträgliche Chrome-Repaints zur Korrektur einer falschen Zeichnungsreihenfolge,
- selbst gezeichnete Rahmen oder ASCII-Pane-Simulationen.

## Verbindliche Architektur

### Verantwortlichkeiten

| Objekt | Verantwortung | Keine Verantwortung |
|---|---|---|
| `MRBentoBox` | generischer Layoutbaum, Leaf-Lebensdauer, Standard-Bento-Chrome, TVision-Fokus und Ereignisrouting | Hex-Rollen, Zahlenbasen, Binärspeichern, Inspector-Werte oder String-Erkennung |
| `MRBentoHexEditor : MRBentoBox` | Hex-spezifische Pane-Spezifikation, kanonischer Byte-Cursor, Bearbeitungssemantik, Inspector- und String-Projektion | Rahmenzeichnen, Splitter, Z-Order oder Pane-Lebensdauer außerhalb seiner eigenen Pane-Fabrik |
| `MRHexPaneWindow : MRPaneEditWindow` | echter Pane-Host für genau eine Hex-Rolle; lokaler Canvas, lokale Eingabeweitergabe und Inhaltssichtbarkeit | Dokumentbesitz oder selbstständige Cursor-Wahrheit |
| `MRHexPaneView : TView` | Darstellung innerhalb des Client-Extent seines `MRHexPaneWindow` | Pane-Rahmen, Titel, globale Geometrie, direkte Bildschirmzugriffe oder Dokumentmutationen |
| bestehender `MRFileEditor` mit `MRTextDocument`/AddBuffer | kanonische Bytefolge, Undo/Redo, Insert, Erase, Replace und Speichern | Zahlenformat-Projektion oder Hex-Feldvalidierung |

### Einheitliche Pane-Struktur

Alle sechs Rollen sind echte, gleichartige `MRHexPaneWindow`-Instanzen:

```text
MRBentoHexEditor
  +-- MRHexPaneWindow  HEX
  +-- MRHexPaneWindow  Strings
  +-- MRHexPaneWindow  Inspector
  +-- MRHexPaneWindow  Decimal
  +-- MRHexPaneWindow  Binary
  +-- MRHexPaneWindow  Octal
```

Der Primär-Leaf ist dabei keine Ausnahme. Er erhält ebenfalls einen dedizierten
Pane-Host. Der äußere `MRBentoHexEditor` bleibt Dokument- und Bento-Container,
nicht Rendering-Canvas der Rolle `HEX`.

`MRBentoBox` darf für den Primär-Leaf weiterhin den bisherigen Dokument-Host
verwenden. Nur eine abgeleitete BentoBox darf über einen allgemeinen,
rollenagnostischen Vertrag einen dedizierten Primär-Pane-Host verlangen. Dieser
Vertrag muss auch den bestehenden Pane-Fabrikpfad benutzen. Die BentoBox darf
nicht auf `bbmHexEditor`, `bprHex`, `bprBinary` oder andere Hex-Rollen schalten.

## Wiederverwendung und zulässige Parent-Erweiterung

Die vorhandene Pane-Erzeugung von `MRBentoBox` wird für alle sechs Pane-Hosts
benutzt. Falls der heutige Factory-Pfad Leaf 0 noch nicht erzeugt, wird er
generisch auf Leaf 0 erweitert. Zulässig sind ausschließlich stabile,
rollenagnostische Protected-Extension-Points:

- Erzeugung eines `MRPaneEditWindow` für einen Leaf,
- Kennzeichnung, ob ein abgeleiteter Bento-Typ für seinen Primär-Leaf einen
  dedizierten Pane-Host benutzt,
- generische Auflösung des Pane-Hosts eines Leafs für Layout und Ereignisrouting.
- Auslösung einer bereits bestehenden, rollenagnostischen Pane-Content-Projektion
  für abgeleitete Darstellungen nach einer Laufzeitkonfigurationsänderung.

Die endgültigen Methodennamen werden beim erneuten Audit der bestehenden
Fabrik festgelegt. Es werden keine Hex-Namen, Hex-Modi, Hex-Rollen oder
Hex-Draw-Zweige in `MRBentoBox` eingeführt. Neue Registries, generische
Framework-Abstraktionen, parallele Pane-Fabriken und globale Zustände sind
ausgeschlossen.

## Layout und TVision-Projektion

Das initiale Layout ist fest als Würfel-Sechs anzulegen:

```text
[ HEX     ][ Strings ][ Inspector ]
[ Decimal ][ Binary  ][ Octal     ]
```

- Rahmen, Titel, Fokus, Splitter und Resize stammen ausschließlich aus dem
  vorhandenen Bento-Mechanismus.
- Jeder `MRHexPaneView` wird nur vom zugehörigen `MRHexPaneWindow` gezeichnet.
- Jede Draw-Methode schreibt ausschließlich in ihren lokalen `TView`-Extent.
- Der Hex-Editor zeichnet weder Inhalte eines fremden Pane noch danach erneut
  Bento-Chrome.
- Fokuswechsel und Mausereignisse laufen zuerst über den vorhandenen
  Bento-Leaf-Router; der Pane-Host erhält nur lokale Ereignisse seines Leafs.

## Datenmodell und Bearbeitung

- Es wird die bestehende `MRTextDocument`-/AddBuffer-Kombination benutzt.
  Es gibt keine abgeleitete PieceTable und keinen zweiten Bytebuffer.
- Der kanonische Cursor ist ein Byte-Offset im `MRBentoHexEditor`; alle
  Daten-Panes projizieren ihn als blinkenden Blockcursor, der Inspector
  verwendet ihn ausschließlich für seine read-only Interpretation.
- Hex, Decimal, Binary, Octal und Strings sind editierbar. Die numerischen
  Felder validieren beim Commit ihren Wertebereich `0..255`.
- `Esc` verwirft den Feldpuffer. Ein Klick außerhalb des aktiven Felds und ein
  Panewechsel committen einen gültigen Feldwert; ungültige numerische Werte
  behalten den Fokus bis zur Korrektur oder zu `Esc`.
- Insert und Overwrite verwenden ausschließlich die bestehenden
  `replaceRangeAndSelect`-/Dokumentoperationen.
- `Ins` schaltet den vorhandenen Insert-Modus. Der Modus gilt kanonisch für
  den Hex-Container, nicht für einen versteckten Pane-Editor.
- `Ctrl+G` akzeptiert Dezimal, `0x` und `0o`; `Ctrl+E` schaltet Little/Big
  Endian für den Inspector.
- `BINARY_RECORD_LENGTH` bestimmt die Bytezahl je Record und wird nach
  erfolgreichem Abschluss von Setup- und Profil-Dialogen hot projiziert.

## Speichern und Stringumfang

- Der Hex-Modus aktiviert für das aktuelle Dokument einen lokalen,
  nicht persistierten bytegenauen Speichermodus. Weder `FILE_TYPE` noch
  Profil- oder Settings-Persistenz werden verändert.
- Der erste Lieferumfang zeigt C-Strings und sichere ASCII-/UTF-8-Spannen.
  Unbekannte Spannen bleiben ausgeblendet.
- Pascal, BSTR, Java-MUTF-8, .NET-, Big5-, Shift-JIS- und GB18030-Formate
  werden nicht blind erkannt. Sie bleiben ein Addendum nach visueller
  Freigabe mit gewähltem Encoding-/Dateiformatprofil.

## Tranches

1. **Bento-Audit und generischer Primär-Leaf-Host**
   - Dateien: `ui/MRBentoBox.hpp`, `ui/MRBentoBoxProjection.cpp`.
   - Entfernt jede Hex-Kenntnis aus dem Parent.
   - Liefert keine Hex-Darstellung, nur die generische Host-Invariante.

2. **Sechs echte Hex-Panes**
   - Dateien: `ui/hex/MRBentoHexEditor.hpp/.cpp`,
     `ui/hex/panes/MRHexPaneWindow.hpp/.cpp`,
     `ui/hex/panes/MRHexPaneView.hpp/.cpp`, `Makefile`.
   - Jede Source bleibt unter 1000 Zeilen. Semantische Aufteilungen erfolgen
     nur bei einer stabilen Fachgrenze.

3. **Byte-Editierung, Inspector und Save-Modus**
   - Dateien: Hex-Panes sowie nur die bereits bestehenden lokalen
     `MRFileEditor`-Speicheroptionen, falls bytegenaues Speichern dort nicht
     ohne Erweiterung ausgedrückt werden kann.
   - Kein Settings-, Bootstrap- oder Persistenzumbau.

4. **Hot-Projektion, Workspace und Sichtprüfung**
   - Bestehende Setup-/Profil-Abschlusswege aktualisieren nur den sichtbaren
     Hex-Editor.
   - Workspace nutzt den bestehenden Bento-Snapshottransport; keine neue
     Serialisierung.

## Risiko, Prüfungen und Freigabepunkte

| Risiko | Gegenmaßnahme |
|---|---|
| TVision-Lebensdauer und Z-Order | Jeder Pane-Host wird vom Bento-Container eingefügt und zerstört; keine Fremd-Overlays oder nachträglichen Repaints |
| Primär-Leaf-Regression für bestehende BentoBoxen | Der neue Parent-Vertrag bleibt im Standardpfad deaktiviert; Dokument- und File-Compare-Bentos behalten ihren bisherigen Primär-Host |
| Byteverlust beim Speichern | Binärmodus nur lokal am Hex-Dokument; Bytevergleich vor/nach Save-As und Save |
| Cursor-Divergenz | Ein kanonischer Offset, keine Cursor-Wahrheit in Pane-Editoren |

Vor jeder Implementierungstranche werden die betroffenen Schutzverträge und
der konkrete Diff erneut geprüft. Nach jeder Tranche sind vorgesehen:

1. Code-Sichtprüfung der Eigentums- und Draw-Grenzen,
2. `make clean all CXX=clang++`,
3. relevante Regressionen,
4. manuelle Prüfung von Fokus, Splittern, Resize, Editieren, `Esc`, Insert,
   Goto, Endian, Save und Record-Length-Projektion.
