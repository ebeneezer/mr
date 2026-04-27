# MR / MRMAC – Planungsnotizen zum Key-Mapping-System

## Zielsetzung

Für MR/MRMAC soll ein profilfähiges Key-Mapping-System geplant werden, das sich an den kanonischen MRMAC-Funktionen der Referenz orientiert. Es soll nicht nur WordStar-artige Belegungen unterstützen, sondern grundsätzlich auch andere Bedienmodelle wie Emacs oder Nano. Es muss möglich sein eine beliebig lange Folge von Steuerungstasten einer Funktion zuzuordnen: Beispiel Ctrl-K-V für Blockmove bei WordStar. Emacs kennt noch längere Steuerungstastensequenzen.

Wesentliche Leitlinie:

> Die Funktionen müssen an genau einer Stelle modelliert, geladen, validiert und zur Laufzeit aufgelöst werden. Menü, Statuszeile, Hilfetext und Dialog sind nur Ansichten darauf.

---

## 1. Kanonischer Funktionskatalog

- Grundlage ist ausschließlich die MRMAC-Referenz.
- Diese Funktionen bilden den verbindlichen Codex.
- Erweiterungen werden nicht vorweggenommen.
- Profile dürfen diese Funktionen nur binden, aber nicht redefinieren.
- Die semantische Bedeutung von beispielsweise `MRMAC_FILE_SAVE`, `MRMAC_CURSOR_UP`, `MRMAC_BLOCK_DELETE` usw. ist zentral und starr.
- Keymapping-Profile definieren nur die Zuordnung von Tasten bzw. Präfixen auf diese kanonischen Funktionen oder auf Makros.

Wichtige Präzisierung aus der Diskussion:

- Es wird nicht mehr von `MEMAC_*`, sondern von `MRMAC_*` gesprochen.
- Der kanonische Katalog ist die erste Schicht.
- MR-spezifische Erweiterungen werden als zweite, explizite Schicht modelliert und nicht in den kanonischen Katalog hineingemischt.
- Daraus folgt:
  - `MRMAC_*` = referenzgebundene kanonische Funktionen
  - `MR_*` = echte MR-Erweiterungen
- Profile dürfen auf beide Schichten binden, aber die Herkunft einer Funktion muss sichtbar bleiben.

---

## 2. Profilmodell statt Einzelbelegung

Key-Mapping wird von Anfang an als **Profilproblem** modelliert.

Beabsichtigte Profile:

- WordStar
- Emacs
- Nano
- benutzerdefinierte Profile

Planungsnotiz aus der Diskussion:

- Das bereits jetzt gültige interne Default-Set soll später als editierbares, aber nicht löschbares Profil `DEFAULT` sichtbar werden.
- `DEFAULT` ist damit Baseline und Fallback, nicht ein unsichtbarer Sonderfall.
- Diese Notiz ist bewusst **noch keine** Detailfestlegung des Dialogverhaltens, sondern eine gesetzte Richtung.

`vi` wird zunächst bewusst verschoben.

Begründung:

- `vi` ist nicht nur ein anderes Mapping, sondern ein modaler Zustandsautomat.
- Das würde in dieser Phase unnötig viel Churn in Datenmodell, Event-Routing und Statuslogik erzeugen.
- Wenn Kontexte bereits sauber vorgesehen werden, kann `vi` später ohne architektonischen Bruch ergänzt werden.

---

## 3. Trennung der Ebenen

### 3.1 Kanonische Aktion

Die Codebase kennt feste kanonische MRMAC-Funktionen.

### 3.2 Key-Mapping-Profil

Ein Profil ordnet Aktionen pro Kontext konkrete Tasten oder Tastensequenzen zu.

### 3.3 Laufzeitkontext

Bindungen gelten nicht zwangsläufig überall gleich. Daher wird ein Kontextbegriff vorgesehen, etwa:

- `EDIT`
- `DIALOG`
- `READONLY`
- `LIST`

Spätere Erweiterungen bleiben möglich.

Präzisierung aus der Diskussion:

- Der Resolver soll pro Tastenevent genau **einen** aktiven Laufzeitkontext liefern.
- Mehrdeutigkeiten werden nicht erst spät in der Aktionsauflösung behandelt.
- Falls orthogonale Kontexte intern erhalten bleiben, braucht es trotzdem einen zentralen Resolver mit deterministischer Prioritätsregel.
- Die erste Phase darf klein starten, aber der Resolver muss schon jetzt als zentrale Stelle vorgesehen werden.

---

## 4. Serialisierung ausschließlich über `MRSETUP(...)`

Strategisch gesetzt ist:

- Es wird **keine neue Direktive** eingeführt.
- Die Serialisierung erfolgt in `settings.mrmac` über `MRSETUP(...)`.
- Beispiel für das aktive Profil:
  `MRSETUP('ACTIVE_KEYMAP_PROFILE', 'name="WORDSTAR" file="/home/idoc/mr/mrmac/macros/wordstar.mrmac"')`
- Schlägt das Laden fehl, wird das Default-Keymapping aktiviert und eine Error-Nachricht im Log und auf der Messageline in Error-Colorierung emittiert.
- Keymapping-Profile dürfen analog zu Color Themes in `.mrmac` Dateien gespeichert und von dort geladen werden. Sie werden instantan angewendet.

Beispielhafte Richtung:

```text
MRSETUP('KEYMAP_PROFILE', 'name="WORDSTAR" description="WordStar controls"');
MRSETUP('KEYMAP_BIND', 'name="WORDSTAR" context=EDIT type=Action target="MRMAC_FILE_SAVE" sequence="<Ctrl+K> <Ctrl+S>" description="Save file"');
MRSETUP('KEYMAP_BIND', 'name="WORDSTAR" context=EDIT type=Action target="MRMAC_CURSOR_UP" sequence="<Ctrl+E>" description="Cursor up"');
```

Wesentlich ist:

- `KEYMAP_PROFILE` und `KEYMAP_BIND` dürfen mehrfach vorkommen.
- Diese Records werden beim Laden als **Liste** gesammelt.
- Der bootstrap baut einen Subhash im zentralen K/V Hash für die Keymappings auf. Die Erkennung geschieht über endliche Automaten.
- Der zentrale K/V Hash ist die **einzige** editierbare und persistente Wahrheit des Keymapping-Systems.
- Strukturierte Profilobjekte sind nur Sichten auf diese Daten und keine zweite Registry.
- Ein Binding ist inhaltlich identifizierbar, ohne dass seine Position in der Datei eine Rolle spielen muss.
- Validierung erkennt Konflikte explizit.
- Doppelte Bindungen (gleiche Sequenz, gleiche Funktion) überschreiben silent, so dass die nächste Serialisierung von `settings.mrmac` die Dopplung beseitigt.
- Mehrere Auslöser für dieselbe Funktion sind ausdrücklich zulässig. Beispiel:
  - `BLOCK_BEGIN` auf `Ctrl-F7`
  - `BLOCK_BEGIN` auf `F12`
  Das ist **kein Konflikt**.

---

## 5. Präfixmodell und Sequenzauflösung

Der Präfixfall ist kein Randdetail, sondern der Kern des Systems.

Konsens aus der Diskussion:

- Es wird **kein Timeout** verwendet.
- Das Modell orientiert sich an Emacs-artiger Präfixauflösung, nicht an Zeitfenstern.
- Es gibt einen zentralen Pending-State für begonnene Präfixsequenzen.
- Die Auflösung erfolgt über einen Präfixbaum / Trie bzw. einen endlichen Automaten.
- `Esc` ist der Default-Abbruch für einen offenen Präfixzustand.
- Der Abort-Key soll später registrierbar sein.

Verbindliche Regeln für Phase 1:

- Eine Sequenz ist entweder ein **terminales Binding** oder ein **reiner Präfixknoten**.
- Dieselbe Sequenz darf im selben Kontext **nicht gleichzeitig** als vollständiges Binding und als Präfix für längere Sequenzen existieren.
- Ein Prefix-only / Prefix-and-Complete-Mischfall ist in Phase 1 **unzulässig**.
- Bei ungültiger Fortsetzung wird die laufende Pending-Sequenz verworfen und eine Warning/Error auf der Messageline emittiert.
- Der letzte fehlerhafte Tastenschritt wird in Phase 1 nicht automatisch als neue Root-Sequenz recycelt.

Konsequenz:

- WordStar-artige Familien wie `Ctrl-K`, `Ctrl-Q` lassen sich sauber modellieren.
- Emacs-artig lange Sequenzen bleiben möglich.
- Mehrdeutigkeit wird konstruktiv ausgeschlossen statt heuristisch aufgelöst.


## 9. Validierungsregeln für Key-Mapping-Profile

### Regel 1: Profil-ID muss eindeutig sein

Jedes Profil braucht eine eindeutige Profil-ID.

Unzulässig:

- dasselbe Profil zweimal definieren

### Regel 2: Jeder Binding-Record muss genau ein Ziel haben

Ein Binding darf genau eine Zielart besitzen:

- direkte kanonische Funktion
- Makro

Keine Mischformen.

### Regel 4: Makroziele müssen referenziell auflösbar sein

Wenn ein Binding auf ein Makro zeigt, muss dieses Makro existieren und ladbar sein.

Konsens:

- Ein ungültiges Makroziel wird **nicht** still entfernt.
- Das Binding wird im Key Manager als **invalid** markiert.
- Zusätzlich erfolgt eine Fehlermeldung in Richtung Log und Messageline.
- Das Profil bleibt ladbar, aber mit explizit sichtbarem Fehlerzustand.

### Regel 5: Sequenzen werden vor Vergleichen kanonisch normalisiert

- Zur Definition von Bindungs wird ein neuer Dialog mit dem Subeintrag "Key mappings" ausgelöst. Als Orientierung hier ein Screenshot vom Original: documentation/pngsjpegs/Bildschirmfoto_20260408_204423.png
- Die Registrierung einer Funktion geschieht über die bereits implementierte Key-Capture-Programmlogik. Der Macro-Manager-Dialog selbst wird dafür **nicht** benutzt.
- Falls sinnvoll, wird die Capture-Programmlogik zentralisiert, so dass Macro Manager und Key Manager dieselbe Erfassungslogik konsumieren.
- Im Dialog wird eine Liste der Funktionen zu ihren Key-Sequenzen verwaltet (Plural ist hier korrekt).
- Sequenzen können gelöscht werden, aber jede kanonische Funktion aus MRMAC muss in der Liste einmal vorkommen, auch wenn ihr keine Sequenz zugewiesen ist.
- Der Satz der zuerst zu belegenden Funktionen ist in `documentation/pngsjpegs/Bildschirmfoto_20260426_121232.png` definiert.
- Die Typologie und Darstellung der Sequenzen beginnt mit `documentation/pngsjpegs/Bildschirmfoto_20260426_121401.png`.
- Nach dem Recording einer Sequenz für eine Funktion ist die Sequenz in genau dieser kanonischen Form darzustellen.


### Regel 8: Mehrere verschiedene Auslöser auf dieselbe Funktion sind zulässig

Beispiel:

- `Ctrl-F7 -> BLOCK_BEGIN`
- `F12 -> BLOCK_BEGIN`

Das ist erlaubt.

### Regel 11: Kontext trennt Bindings sauber

Dieselbe Taste darf in verschiedenen Kontexten unterschiedliche Funktionen haben.

Beispiel:

- `EDIT | F3 -> SEARCH`
- `DIALOG | F3 -> DELETE_HISTORY_ENTRY`

### Regel 12: Nur bekannte Kontexte sind zulässig

Für die erste Phase werden nur fest definierte Kontexte erlaubt.


## 11. Vorläufige Planrichtung

### Phase 1

- Kanonischen MRMAC-Funktionskatalog aus der Referenz festziehen
- Keymapping-Profilmodell definieren
- Serialisierung ausschließlich über `MRSETUP(...)`
- Listen-Semantik für `KEYMAP_PROFILE` / `KEYMAP_BIND` sauber modellieren
- Präfixregeln ohne Timeout definieren
- Kontext-Resolver definieren
- Strikte Sequenztypologie definieren
- Trennung `MRMAC_*` vs. `MR_*` festziehen

### Phase 2

- aktives Profil laden und anwenden
- direkte Einzeltastenbindungen
- Makroziele im Profil erlauben
- Makrokatalogdateien mit mehreren Makros verwenden

---

## 12. Konsensstand der Diskussion

Der aktuelle Konsens lautet:

- Terminologie ab jetzt `MRMAC`, nicht `MEMAC`.
- Es gibt keinen Timeout für Präfixsequenzen.
- Default-Abbruch für laufende Präfixfamilien ist `Esc`.
- Prefix-only- und Prefix-plus-terminal-Mischfälle sind in Phase 1 unzulässig.
- Der Laufzeitkontext muss zentral und deterministisch aufgelöst werden.
- Der kanonische Katalog und MR-Erweiterungen werden explizit getrennt modelliert.
- Ungültige Makroziele werden im Key Manager als invalid markiert und nicht still entfernt.
- Die vorhandene Key-Capture-Logik darf wiederverwendet werden, aber nicht über den Macro-Manager-Dialog.
- Ein neuer Top-Level-Ordner `~/mr/keymap` ist akzeptiert.
- `app/commands` wird **nicht** zum Hauptablageort des Keymapping-Systems.
- Ein breiter Strukturumbau von `commands` oder `utils` ist in diesem Zug **nicht** beschlossen.

---

## 13. Nächste Züge

Die nächsten Züge werden bewusst in fachlich enger Reihenfolge definiert:

### Zug 1: Kanonischen MRMAC-Funktionssatz festziehen

- Grundlage ist `documentation/pngsjpegs/Bildschirmfoto_20260426_121232.png`.
- Jede dort definierte Funktion erhält eine stabile kanonische ID.
- Noch nicht behandelt werden freie Erweiterungen außerhalb dieses Satzes.

### Zug 2: Sequenztypologie festziehen

- Grundlage ist `documentation/pngsjpegs/Bildschirmfoto_20260426_121401.png`.
- Tastenelemente werden in eine strenge kanonische Tokenform überführt.
- Normalisierung und Stringdarstellung müssen daraus eindeutig folgen.

### Zug 3: Präfixautomat spezifizieren

- Definition von terminalen Bindings
- Definition von Präfixknoten
- Pending-State
- Abort-Verhalten
- Fehlerverhalten bei ungültiger Fortsetzung

### Zug 4: Kontext-Resolver spezifizieren

- Welche Kontexte Phase 1 kennt
- Wie genau ein Event in genau einen Laufzeitkontext aufgelöst wird
- Welche Priorität oder welche Konstruktionsregel Mehrdeutigkeiten ausschließt

### Zug 5: Zweischichtiges Aktionsmodell definieren

- `MRMAC_*` als kanonische Schicht
- `MR_*` als MR-Erweiterungsschicht
- Regeln, welche UI diese Herkunft sichtbar machen muss

### Zug 6: Dateischnitt für `~/mr/keymap` festziehen

Vorläufige Richtung:

- `keymap/MRKeymapActionCatalog.*`
- `keymap/MRKeymapToken.*`
- `keymap/MRKeymapSequence.*`
- `keymap/MRKeymapProfile.*`
- `keymap/MRKeymapResolver.*`
- `keymap/MRKeymapContext.*`
- `keymap/MRKeymapCapture.*`

Diese Dateiliste ist noch eine Planungsrichtung und kein automatisch freigegebener Refactor.


## 14. Zusammenfassung

Das geplante System soll:

- auf einem festen MRMAC-Codex beruhen
- profilebasiert sein
- ausschließlich über `MRSETUP(...)` serialisieren
- semantisch reihenfolgeunabhängig laden
- Konflikte explizit validieren
- Mehrfachbindungen pro Funktion erlauben
- Präfixfamilien etwa `CTRL-K` für WordStar-Blockkommandos sauber unterstützen
- MR-Erweiterungen explizit als zweite Schicht führen
- ohne Timeout arbeiten
- einen zentralen Kontext-Resolver und einen zentralen Präfixzustand besitzen

---

## 15. Gesamtumfang der sieben neuesten Original-Screenshots

Die sieben neuesten Dateien in `documentation/pngsjpegs` zeigen den historischen Gesamtumfang des Original-Keymaps. Dieser Gesamtumfang ist für MR fachlich **nicht** vollständig als Phase-1-Ziel zu verstehen. Er dient hier zunächst als Material für eine geordnete Paketierung.

Sichtbare Unterteilungen:

- Help / Menüs / Meta
- File operations
- Search and replace
- Block operations
- Window operations
- Deleting text
- Cursor movement
- Text manipulation
- Macro operations
- Misc operations
- Communications module keymap
- VCS specific keymap

Zu den Unterteilungen gehören im Original unter anderem:

- Help / Menüs / Meta: Help-Index, vorige Hilfethemen, Menüeinstiege, Mausfunktionen, Cut/Paste aus der Hilfe, Suchfunktionen auf markierte Wörter.
- File operations: Dateioperationen des Originalsystems als eigener Funktionsblock.
- Search and replace: Vorwärts-/Rückwärtssuche, Replace-Varianten, Wiederholung der letzten Suche, Multi-file-Suche, Trefferlisten.
- Block operations: Block markieren, Column/Stream-Block, Copy/Move/Delete, Buffer-Transfers, Block-Math, markierende Cursor-Shift-Familien.
- Window operations: Fensterwechsel, Fensterliste, neues Fenster, Resize, Link, Close, Zoom, Minimize, Split.
- Deleting text: Delete-to-end-of-line, Delete-char, Delete-word, Backspace-Varianten, Delete-line, Undo-Familie.
- Cursor movement: Pfeiltastenfamilie, Home/End, Page up/down, Datei-Anfang/Ende, Wortsprünge, Window-Top/Bottom, Scroll, Marks, Go-to-line, Indent/Undent, Center line.
- Text manipulation: Paragraph-Reformat, Justify/Unjustify, Center line, Upper/Lower case, Date/Time stamp, Page break, Enter/Open line, Insert mode, Ruler.
- Macro operations: Makro ausführen, Key-Record.
- Misc operations: Notebook, Version control, Layout dialog box, File compare, Session manager, ASCII table, File manager, DOS shell, Compile, Next compiler error, Template expansion, Match brace, Calculator, Spell check, Phone/Address list.
- Communications module keymap: Kommunikationsmodul, Datei-Transfer, Log-Funktionen, Dial/Hangup, Setup-Menüs, Help-Menüs.
- VCS specific keymap: VCS-Integration, Checkout/Update/Unlock, History, Visual compare, Configure, interactive VCS, SourceSafe user change.

Fachliche Einordnung:

- Für MR Phase 1 sind `Communications module keymap` und `VCS specific keymap` derzeit klar nachrangig.
- Auch große Teile von `Misc operations` sind eher historischer Ballast als ein sinnvoller Startpunkt.
- Für einen ersten tragfähigen Ausbau sind die editornahen Grundfamilien wichtiger als die exotischen oder externen Integrationsblöcke.

Empfohlene Paketierung:

1. Cursor movement
   Begründung: klein, gut beobachtbar, hoher Alltagsnutzen, geringe Modellkomplexität.
2. Deleting text
   Begründung: eng verwandt mit Cursorsteuerung und ebenfalls editorischer Kernbestand.
3. Search and replace
   Begründung: fachlich zentral, aber bereits komplexer wegen Dialogen, Richtungen und Wiederholungslogik.
4. Block operations
   Begründung: wichtig für WordStar-/MRMAC-Nähe, aber erst sinnvoll, wenn Cursor- und Delete-Grundlagen stabil sind.
5. Window operations
   Begründung: MR-relevant, aber stärker an bestehende App-Kommandos und UI-Kontexte gekoppelt.
6. Text manipulation
   Begründung: sinnvoll, aber für Phase 1 weniger grundlegend als Navigation, Delete, Suche und Blockarbeit.
7. Macro operations
   Begründung: architektonisch wichtig, aber erst nach stabiler Token-, Sequenz- und Capture-Basis sinnvoll.
8. File operations
   Begründung: relevant, aber oft enger an Menüs, Dialoge und bestehende MR-Kommandos gekoppelt.
9. Help / Menüs / Meta
   Begründung: eher Ableitung und Sichtbarkeit des Systems als Kern des Dispatch-Modells.
10. Misc operations
    Begründung: nur selektiv und nach echtem Bedarf.
11. Communications module keymap
    Begründung: aktuell kein realistischer Frühphasenfokus.
12. VCS specific keymap
    Begründung: historisch interessant, für MR-Keymapping aber derzeit klar außerhalb des Startkerns.

Empfehlung für den Einstieg:

- Beginn mit `Cursor movement`.
- Danach `Deleting text`.
- Anschließend `Search and replace` in einer bewusst kleinen Kernmenge.

Diese Reihenfolge ist nicht nur bequem, sondern fachlich sauber: erst einfache, hochfrequente Primitive; dann editorische Basiskommandos; erst danach komplexere Familien mit mehr Zustands- und Dialoglogik.

---

## 16. Zug 1 ausgearbeitet für Pakete 1 bis 4

In diesem Zug wird **noch keine Tastenzuordnung** festgelegt. Es wird nur der kanonische MRMAC-Funktionssatz für die ersten vier Pakete festgezogen.

Leitregeln:

- Die kanonische ID ist stabil und sprachneutral gegenüber späteren Key-Profilen.
- Die Originalbeschriftung bleibt nur als historische Referenz erhalten.
- Wo das Original semantisch unscharf ist, wird dies offen markiert und **nicht** schöngeredet.
- Wo eine Funktion zusätzliche Eingabe erwartet, gehört diese Zusatz-Eingabe **nicht** in die Funktions-ID.

### 16.1 Paket 1: Cursor movement

Kanonische Funktionen:

- `MRMAC_CURSOR_LEFT` - Original: `Cursor left`
- `MRMAC_CURSOR_RIGHT` - Original: `Cursor right`
- `MRMAC_CURSOR_UP` - Original: `Cursor up`
- `MRMAC_CURSOR_DOWN` - Original: `Cursor down`
- `MRMAC_CURSOR_HOME` - Original: `Cursor to home`
- `MRMAC_CURSOR_END_OF_LINE` - Original: `Cursor to end of line`
- `MRMAC_VIEW_PAGE_UP` - Original: `Display page up`
- `MRMAC_VIEW_PAGE_DOWN` - Original: `Display page down`
- `MRMAC_CURSOR_TOP_OF_FILE` - Original: `To top of the file`
- `MRMAC_CURSOR_BOTTOM_OF_FILE` - Original: `To bottom of the file`
- `MRMAC_CURSOR_NEXT_PAGE_BREAK` - Original: `Cursor to next page break`
- `MRMAC_CURSOR_PREV_PAGE_BREAK` - Original: `Cursor to last page break`
- `MRMAC_CURSOR_WORD_LEFT` - Original: `Cursor word left`
- `MRMAC_CURSOR_WORD_RIGHT` - Original: `Cursor word right`
- `MRMAC_CURSOR_TOP_OF_WINDOW` - Original: `Top of window`
- `MRMAC_CURSOR_BOTTOM_OF_WINDOW` - Original: `Bottom of window`
- `MRMAC_VIEW_SCROLL_UP` - Original: `Scroll window up`
- `MRMAC_VIEW_SCROLL_DOWN` - Original: `Scroll window down`
- `MRMAC_CURSOR_START_OF_BLOCK` - Original: `Cursor to start of block`
- `MRMAC_CURSOR_END_OF_BLOCK` - Original: `Cursor to end of block`
- `MRMAC_CURSOR_GOTO_LINE` - Original: `Move cursor to line num`
- `MRMAC_CURSOR_INDENT` - Original: `Indent`
- `MRMAC_CURSOR_TAB_RIGHT` - Original: `Tab right`
- `MRMAC_CURSOR_TAB_LEFT` - Original: `Tab left`
- `MRMAC_CURSOR_UNDENT` - Original: `Undent`
- `MRMAC_MARK_PUSH_POSITION` - Original: `Mark position on stack`
- `MRMAC_MARK_POP_POSITION` - Original: `Get position from stack`
- `MRMAC_MARK_SET_RANDOM_ACCESS` - Original: `Set a random access mark`
- `MRMAC_VIEW_CENTER_LINE` - Original: `Center line on screen`
- `MRMAC_MARK_GET_RANDOM_ACCESS` - Original: `Get random access mark`

Wichtige fachliche Festlegungen:

- `MRMAC_CURSOR_GOTO_LINE` erwartet einen numerischen Parameter; die Zahl ist kein Teil der Funktions-ID.
- `MRMAC_MARK_SET_RANDOM_ACCESS` und `MRMAC_MARK_GET_RANDOM_ACCESS` erwarten einen nachgelagerten Mark-Identifier; auch dieser ist kein Teil der Funktions-ID.
- `VIEW_*` wird bewusst dort verwendet, wo das Original eher die sichtbare Ansicht verschiebt als nur den Caret.

### 16.2 Paket 2: Deleting text

Kanonische Funktionen:

- `MRMAC_DELETE_TO_EOL` - Original: `Delete to end of line`
- `MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK` - Original: `Del character (or block)`
- `MRMAC_DELETE_FORWARD_WORD` - Original: `Delete word forward`
- `MRMAC_DELETE_BACKWARD_CHAR` - Original: `Back space`
- `MRMAC_DELETE_BACKWARD_WORD` - Original: `Backspace a whole word`
- `MRMAC_DELETE_LINE` - Original: `Delete line`
- `MRMAC_DELETE_BACKWARD_TO_HOME` - Original: `Backspace to home`
- `MRMAC_UNDO` - Original: `Undo`
- `MRMAC_REDO_LAST_UNDO` - Original: `Undo your last undo`

Wichtige fachliche Festlegungen:

- `Del character (or block)` ist bewusst nicht auf `CHAR` verkürzt, weil die Originalfunktion blocksensitives Verhalten hat.
- `Undo your last undo` wird nicht weichgespült, sondern als Redo-artige Semantik explizit benannt.

### 16.3 Paket 3: Search and replace

Kanonische Funktionen:

- `MRMAC_SEARCH_FORWARD` - Original: `Search`
- `MRMAC_SEARCH_REPLACE` - Original: `Search and replace`
- `MRMAC_SEARCH_REPEAT_LAST` - Original: `Repeat last Search/Replac`
- `MRMAC_SEARCH_MULTI_FILE` - Original: `Multi file search`
- `MRMAC_SEARCH_LIST_MATCHED_FILES` - Original: `List matched files`

Wichtige fachliche Festlegungen:

- `MRMAC_SEARCH_REPEAT_LAST` ist eine echte Funktion und kein bloßer UI-Shortcut.
- `MRMAC_SEARCH_MULTI_FILE` und `MRMAC_SEARCH_LIST_MATCHED_FILES` gehören zwar in den Codex, sind aber **nicht** mehr trivial. Für einen frühen Implementierungszug sollten sie nach dem Suchkern behandelt werden.

### 16.4 Paket 4: Block operations

Kanonische Funktionen:

- `MRMAC_BLOCK_COPY_TO_CLIPBOARD` - Original: `Copy to MS Windows`
- `MRMAC_BLOCK_PASTE_FROM_CLIPBOARD` - Original: `Paste from MS Windows`
- `MRMAC_BLOCK_MARK_CHAR` - Original: `Mark a block`
- `MRMAC_BLOCK_MARK_COLUMN` - Original: `Mark a column block`
- `MRMAC_BLOCK_MARK_STREAM` - Original: `Mark a stream block`
- `MRMAC_BLOCK_CLEAR` - Original: `Turn block mark off`
- `MRMAC_BLOCK_UNDENT` - Original: `Undent block`
- `MRMAC_BLOCK_INDENT` - Original: `Indent block`
- `MRMAC_BLOCK_COPY` - Original: `Copy the marked block`
- `MRMAC_BLOCK_MOVE` - Original: `Move marked block`
- `MRMAC_BLOCK_DELETE` - Original: `Delete marked block`
- `MRMAC_BLOCK_COPY_INTERWINDOW` - Original: `Interwindow block copy`
- `MRMAC_BLOCK_MOVE_INTERWINDOW` - Original: `Interwindow block move`
- `MRMAC_BLOCK_MOVE_TO_BUFFER` - Original: `Move block to buffer`
- `MRMAC_BLOCK_APPEND_TO_BUFFER` - Original: `Append block to buffer`
- `MRMAC_BLOCK_CUT_APPEND_TO_BUFFER` - Original: `Cut and Append Block`
- `MRMAC_BLOCK_COPY_FROM_BUFFER` - Original: `Copy block from buffer`
- `MRMAC_BLOCK_MATH` - Original: `Perform math on block`

Sonderfall `Shift cursor block mark`:

- Das Original listet eine ganze Folge von Einträgen mit derselben Beschriftung `Shift cursor block mark`.
- Diese Originalbeschriftung ist für einen kanonischen Codex fachlich unzureichend, weil sie mehrere unterschiedliche Bewegungsrichtungen hinter demselben Text versteckt.
- Für den Codex wird diese Familie **nicht** als blinder String übernommen.

Vorläufige kanonische Familie:

- `MRMAC_BLOCK_EXTEND_BY_MOTION`

Darunter fallen sichtbar mindestens diese Bewegungsvarianten:

- Left
- Up
- Right
- Down
- End
- PageUp
- Home
- CtrlRight
- CtrlUp
- CtrlLeft
- CtrlDown

Fachliche Bewertung:

- Diese Familie ist **nicht** trivial.
- Für die Implementierung ist später zu entscheiden, ob daraus einzelne feste Aktionen oder eine parametrisierte Bewegungsaktion werden.
- Meine Empfehlung ist hier derzeit **eine parametrisierte Aktionsfamilie**, weil das Original semantisch eine markierende Bewegung ausdrückt und nicht zehn unabhängig gedachte Blockfunktionen.

### 16.5 Kritische Einordnung der Pakete 1 bis 4

Die Paketnummern 1 bis 4 sind zwar als Startbereich akzeptiert, aber sie sind intern nicht gleich schwer:

- `Cursor movement` ist der sauberste Einstieg.
- `Deleting text` ist danach logisch und überschaubar.
- `Search and replace` enthält bereits nichttriviale Zustandslogik.
- `Block operations` ist fachlich deutlich schwerer als die Paketnummer vermuten lässt.

Empfohlene tatsächliche Umsetzungsreihenfolge innerhalb dieser vier Pakete:

1. Paket 1 vollständig
2. Paket 2 vollständig
3. Paket 3 zunächst nur Suchkern: `MRMAC_SEARCH_FORWARD`, `MRMAC_SEARCH_REPLACE`, `MRMAC_SEARCH_REPEAT_LAST`
4. Paket 4 zunächst nur Blockgrundlagen: Markieren, Clear, Copy, Move, Delete
5. Erst danach Paket-3-Rest und Paket-4-Randzonen wie Multi-file-Suche, Clipboard, Buffer-Transfers und Block-Math

---

## 17. Zug 2 ausgearbeitet für Pakete 1 bis 4

In diesem Zug wird die **kanonische Token- und Sequenztypologie** festgezogen. Diese Typologie ist die Basis für:

- Parser
- Serializer nach `settings.mrmac`
- Key-Capture-Normalisierung
- Konfliktprüfung
- Dialoganzeige im Key Manager

### 17.1 Kritischer Befund zur Originaltafel

Die Referenz `documentation/pngsjpegs/Bildschirmfoto_20260426_121401.png` ist als historische Ausgangsbasis nützlich, aber als kanonische MR-Syntax **nicht** direkt übernehmbar.

Gründe:

- Die Tafel ist semantisch uneinheitlich.
  Beispiele: `LF`/`RT` sind kryptisch, `Grey+` mischt Namen und Sonderzeichen.
- Die Tafel ist unvollständig gegenüber den tatsächlich gezeigten Keymaps.
  In den Funktions-Screens erscheinen u.a. `Shift+End`, `Shift+Home`, `Ctrl+Shift+Right`, `Ctrl+Shift+Up`, `GreyEnter`, die auf der Tafel nicht vollständig und systematisch abgebildet sind.
- Die Tafel ist für eine robuste Normalisierung zu implizit.
  Ein modernes MR-Keymapping braucht eine generative Typologie und keine harte Abschrift historischer Tokenlisten.

Folgerung:

- Die Originaltafel definiert die **Klassen der Tasten**.
- Die kanonische MR-Syntax wird daraus **neu und strikt** abgeleitet.
- Falls später historische Schreibweisen importiert werden sollen, dürfen sie nur am Parser-Rand akzeptiert und sofort in die MR-Normalform überführt werden.
- Serialisiert und angezeigt wird **ausschließlich** die MR-Normalform.

### 17.2 Kanonisches Tokenmodell

Ein Key-Token besteht logisch aus:

- null bis drei Modifiers: `Ctrl`, `Alt`, `Shift`
- genau einem Basis-Key

Der Basis-Key gehört genau einer Klasse an:

- Function key
- Navigation key
- Editing key
- Letter key
- Digit key
- Keypad key
- Mouse token

Wichtige Festlegung:

- `Keypad` ist **keine** Modifier-Flag, sondern eine eigene Basis-Key-Klasse.
- `Ctrl+Shift+Right` ist daher ein modifizierter Navigation-Key.
- `Ctrl+KeypadPlus` ist dagegen ein modifizierter Keypad-Key.

### 17.3 Kanonische MR-Serialisierung

Ein einzelnes Token wird in Winkelklammern serialisiert:

- `<Right>`
- `<Ctrl+F5>`
- `<Shift+Tab>`
- `<Ctrl+Shift+Right>`
- `<KeypadEnter>`
- `<Ctrl+KeypadPlus>`

Eine Sequenz aus mehreren Tokens wird durch genau ein ASCII-Space getrennt:

- `<Ctrl+K> <Ctrl+C>`
- `<Ctrl+X> <Ctrl+S>`

Nicht zulässig in der Kanonform sind:

- Komma-getrennte Sequenzen
- mehrfacher Whitespace
- gemischte Groß-/Kleinschreibung
- historische Kurzformen wie `<LF>` oder `<ShftF7>` in der Serialisierung

Empfehlung:

- Die MR-Kanonform soll lesbar sein und nicht museal.
- Daher werden `Right`, `Left`, `PageUp`, `Backspace`, `KeypadPlus` usw. bevorzugt, auch wenn das Original kürzere oder kryptischere Formen benutzt.

### 17.4 Kanonische Reihenfolge der Modifiers

Wenn mehrere Modifiers vorhanden sind, ist die Reihenfolge immer:

1. `Ctrl`
2. `Alt`
3. `Shift`

Beispiele:

- `<Ctrl+Shift+Right>`
- `<Ctrl+Alt+Delete>`
- `<Alt+Shift+F7>`

Nicht kanonisch und daher bei einer späteren Parser-Akzeptanz sofort zu normalisieren wären:

- `<Shift+Ctrl+Right>`
- `<ShftCtrlRT>`
- `<CtrlShftRT>`

### 17.5 Kanonische Basis-Key-Namen

Für Pakete 1 bis 4 werden zunächst diese Basis-Key-Namen festgezogen:

Navigation keys:

- `Left`
- `Right`
- `Up`
- `Down`
- `Home`
- `End`
- `PageUp`
- `PageDown`

Editing keys:

- `Enter`
- `Tab`
- `Esc`
- `Backspace`
- `Insert`
- `Delete`

Function keys:

- `F1` bis `F10`

Letter keys:

- `A` bis `Z`

Digit keys:

- `0` bis `9`

Keypad keys:

- `KeypadPlus`
- `KeypadMinus`
- `KeypadMultiply`
- `KeypadEnter`

Mouse-Tokens:

- `MouseUp`
- `MouseDown`
- `MouseLeft`
- `MouseRight`
- `MouseButtonLeft`
- `MouseButtonRight`
- `MouseButtonMiddle`

Fachliche Einordnung:

- Mouse-Tokens gehören zur Typologie, aber **nicht** zum frühen Implementierungsfokus der Pakete 1 bis 4.
- `F11` und `F12` werden hier bewusst noch nicht festgezogen, weil die Originalreferenz bei `F10` endet. Eine spätere Erweiterung ist möglich, aber dieser Zug soll nicht spekulativ werden.

### 17.6 Normalisierungsregeln

Die Kanonisierung einer Sequenz folgt diesen Regeln:

- Jeder Tokenname wird exakt auf die kanonische Schreibweise normalisiert.
- Modifier werden dedupliziert.
- Modifier werden in die Reihenfolge `Ctrl`, `Alt`, `Shift` sortiert.
- Sequenz-Whitespace wird auf genau ein ASCII-Space reduziert.
- Die Ausgabe verwendet immer die Winkelklammerform.

Beispiele:

- Eingabe ` <ctrl+shift+right> ` -> Ausgabe `<Ctrl+Shift+Right>`
- Eingabe `<ShftTab>` -> Ausgabe `<Shift+Tab>`
- Eingabe `<ctrl+k>    <ctrl+c>` -> Ausgabe `<Ctrl+K> <Ctrl+C>`

Wichtige Festlegung:

- Die MR-Kanonform kennt **genau eine** Schreibweise pro Token.
- Doppelte Alias-Welten sind untersagt.
- Wenn später historische Syntax importiert werden soll, ist das ein Parser-Kompatibilitätszug und **kein** zweites kanonisches Format.

### 17.7 Ableitung für Pakete 1 bis 4

Für die ersten vier Pakete reicht als implementierungsrelevante Tokenoberfläche zunächst:

- reine Navigationstasten:
  `Left`, `Right`, `Up`, `Down`, `Home`, `End`, `PageUp`, `PageDown`
- reine Editing-Tasten:
  `Enter`, `Tab`, `Backspace`, `Insert`, `Delete`, `Esc`
- Function keys:
  `F2`, `F5`, `F6`, `F7`, `F8`
- Modifier-Kombinationen:
  `Ctrl+...`, `Alt+...`, `Shift+...`, `Ctrl+Shift+...`
- Buchstaben-Tasten für WordStar-/MRMAC-Familien:
  insbesondere `B`, `C`, `E`, `G`, `I`, `K`, `T`
- Keypad-Tasten:
  `KeypadPlus`, `KeypadMinus`, `KeypadEnter`

Fachlich wichtig:

- `Ctrl+Shift+Motion` ist **keine** eigene Tokenklasse.
- Es ist eine normale Komposition aus Modifiers plus Navigation-Key.
- Genau das ist der Grund, warum die Typologie generativ und nicht nur tabellarisch sein muss.

### 17.8 Sequenzregeln für Präfixfamilien

Für Zug 2 werden die Sequenzen bereits in ihrer Form festgelegt, auch wenn der Präfixautomat erst in Zug 3 detailliert wird.

Regeln:

- Eine Sequenz besteht aus mindestens einem Token.
- Einzeltastenbindungen sind Sequenzen der Länge 1.
- Mehrtastensequenzen sind geordnete Tokenfolgen.
- Zwischen zwei Tokens steht genau ein ASCII-Space.
- Es gibt keine Sondersyntax nur für Präfixe; Präfixe sind normale Sequenzpräfixe.

Beispiele:

- Einzeltaste: `<F6>`
- Einzeltaste mit Modifier: `<Ctrl+F5>`
- Zweiersequenz: `<Ctrl+K> <Ctrl+C>`
- Zweiersequenz mit gemischten Tokenarten: `<Alt+X> <F2>`

### 17.9 Kritische Empfehlung für die spätere Implementierung

Die Versuchung wird groß sein, die historische Tokenform 1:1 nachzubauen. Das wäre fachlich schwach.

Meine Empfehlung:

- intern strukturiertes Tokenmodell
- genau eine lesbare MR-Kanonform
- optional später ein historischer Importparser

Nicht empfohlen:

- interne Arbeit direkt auf Stringtoken wie `ShftF7`
- Sonderfälle für `Grey+` statt sauberem Keypad-Modell
- parallele Unterstützung mehrerer gleichwertiger Schreibweisen

---

## 18. Zug 3 ausgearbeitet: Präfixautomat

Dieser Zug legt das Laufzeitverhalten für Präfixsequenzen fest. Die Richtung ist nun eindeutig:

- wie WordStar/Emacs
- kein Timeout
- harter Pending-State
- harte Verwerfung bei ungültiger Fortsetzung

### 18.1 Zielmodell

Die Auflösung erfolgt über einen endlichen Automaten bzw. praktisch über einen Präfixbaum pro Laufzeitkontext.

Jeder Knoten ist genau einer dieser Typen:

- `root`
- `prefix`
- `terminal`

Wichtige Festlegung:

- Ein Knoten ist entweder `prefix` oder `terminal`.
- Ein Mischknoten ist in Phase 1 unzulässig.
- Dieselbe Sequenz darf also nicht zugleich vollständige Aktion und Präfix für längere Sequenzen sein.

### 18.2 Laufzeitzustand

Der Resolver hält zentral genau diesen Pending-Zustand:

- aktiver Laufzeitkontext
- bisher erfasste Tokensequenz
- aktueller Präfixknoten

Solange kein Präfix aktiv ist, befindet sich das System logisch im `root`.

Sobald ein erster Tastenschritt auf einen Präfixknoten führt:

- wird der Pending-State eröffnet
- die bisherige Sequenz gespeichert
- auf weitere Eingabe gewartet

### 18.3 Zustandsübergänge

Fall 1: Root nach Terminal

- Eingabe trifft im aktiven Kontext direkt auf eine terminale Sequenz.
- Die zugeordnete Aktion wird sofort ausgeführt.
- Danach wird der Zustand auf `root` zurückgesetzt.

Fall 2: Root nach Prefix

- Eingabe trifft im aktiven Kontext auf einen reinen Präfixknoten.
- Es wird noch nichts ausgeführt.
- Der Pending-State bleibt offen.

Fall 3: Prefix nach Terminal

- Die Fortsetzung vervollständigt eine gültige terminale Sequenz.
- Die Aktion wird ausgeführt.
- Danach wird der Zustand auf `root` zurückgesetzt.

Fall 4: Prefix nach Prefix

- Die Fortsetzung bleibt innerhalb einer gültigen Präfixfamilie.
- Es wird weiter gewartet.
- Der Pending-State bleibt offen.

Fall 5: Prefix nach Invalid

- Die Fortsetzung existiert unter dem aktuellen Präfix nicht.
- Die gesamte Pending-Sequenz wird hart verworfen.
- Es wird keine Aktion ausgeführt.
- Der Zustand geht zurück auf `root`.
- Auf der Messageline wird eine Warning/Error ausgegeben.

### 18.4 Harte Verwerfungsregel

Diese Stelle wird bewusst scharf festgelegt:

- Bei ungültiger Fortsetzung wird die laufende Präfixsequenz vollständig verworfen.
- Der letzte Tastenschritt wird **nicht** als neuer Sequenzanfang recycelt.
- Es gibt kein Backtracking.
- Es gibt kein heuristisches "vielleicht meinte der Benutzer".

Beispiel:

- gültig definiert ist `<Ctrl+K> <Ctrl+C>`
- der Benutzer tippt `<Ctrl+K> <X>`
- Ergebnis:
  die Sequenz wird verworfen, `<X>` wird nicht noch einmal gegen `root` geprüft

Das ist genau die von Ihnen gewünschte harte WordStar-/Emacs-Richtung.

### 18.5 Abort-Verhalten

Default-Abbruch ist:

- `<Esc>`

Regeln:

- Wenn kein Pending-State offen ist, verhält sich `<Esc>` wie sonst im aktiven Kontext definiert.
- Wenn ein Pending-State offen ist und `<Esc>` als Abort-Key registriert ist, wird die Pending-Sequenz verworfen.
- Der Abort selbst löst dann keine weitere Keymap-Aktion aus.
- Danach steht das System wieder im `root`.

Wichtige Festlegung:

- Abort ist ein Resolver-Verhalten und keine normale Zielaktion.
- Der Abort-Key darf später konfigurierbar sein, aber `Esc` ist die Default-Regel.

### 18.6 Keine Timeout-Logik

Es gibt:

- keinen Timeout
- keine automatische Auflösung nach Wartezeit
- keine implizite Annahme, dass eine unvollständige Sequenz "wohl fertig" sei

Folgen:

- Lange Präfixfamilien bleiben möglich.
- Das Modell bleibt deterministisch.
- Die Verantwortung für Eindeutigkeit liegt bei der Bindungsdefinition, nicht bei Zeitfenstern.

### 18.7 Konfliktregeln für Präfixfamilien

Für einen Kontext gilt:

- Eine Sequenz darf genau ein Ziel haben.
- Eine Sequenz darf nicht zugleich `terminal` und `prefix` sein.
- Zwei verschiedene Ziele auf derselben terminalen Sequenz sind ein Konflikt.
- Zwei Präfixfamilien mit identischem Präfixknoten sind nur dann zulässig, wenn sie denselben Präfixbaum fortsetzen und keinen Zielkonflikt erzeugen.

Beispiele:

- zulässig:
  `<Ctrl+K> <Ctrl+C>`
  `<Ctrl+K> <Ctrl+U>`
- unzulässig:
  `<Ctrl+K>` als Aktion
  und zusätzlich `<Ctrl+K> <Ctrl+C>` als längere Sequenz

### 18.8 Resolver-Verhalten bei Kontextwechsel

Wenn sich während eines offenen Pending-State der Laufzeitkontext ändert, gilt:

- die Pending-Sequenz wird verworfen
- der Resolver beginnt im neuen Kontext wieder bei `root`

Begründung:

- Ein Präfix ist kontextgebunden.
- Ein Kontextwechsel mit geschleppter Halbfertig-Sequenz wäre fachlich instabil und schwer erklärbar.

### 18.9 Messageline-Verhalten

Bei ungültiger Fortsetzung oder Abort aus Pending-State:

- keine Dialogbox
- keine Exception-Semantik im UI
- stattdessen kurze Meldung auf der Messageline

Empfohlene Meldungsarten:

- `Unknown key sequence`
- `Key sequence aborted`

Die exakten Texte sind noch kein Teil dieses Zuges. Wichtig ist nur:

- Feedback ja
- Modalität nein

### 18.10 Beispiele

Beispiel A: einfache Einzeltaste

- Binding: `<F6>` -> `MRMAC_SEARCH_FORWARD`
- Eingabe: `<F6>`
- Ergebnis: sofortige Ausführung, kein Pending-State

Beispiel B: gültige Zweiersequenz

- Binding: `<Ctrl+K> <Ctrl+C>` -> `MRMAC_BLOCK_COPY`
- Eingabe: `<Ctrl+K>`
- Zustand: Pending offen
- Eingabe: `<Ctrl+C>`
- Ergebnis: Ausführung, Rückkehr zu `root`

Beispiel C: ungültige Fortsetzung

- Binding: `<Ctrl+K> <Ctrl+C>`
- Eingabe: `<Ctrl+K>`
- Zustand: Pending offen
- Eingabe: `<Ctrl+P>`
- Ergebnis: harte Verwerfung, Meldung, Rückkehr zu `root`

Beispiel D: Abort

- Binding: `<Ctrl+K> <Ctrl+C>`
- Eingabe: `<Ctrl+K>`
- Zustand: Pending offen
- Eingabe: `<Esc>`
- Ergebnis: Pending verworfen, keine Zielaktion, Rückkehr zu `root`

### 18.11 Empfehlung für die Implementierung

Die sauberste spätere Implementierung ist:

- pro Kontext ein Präfixbaum
- strukturierte Knotentypen statt String-Heuristik
- Resolver als kleiner deterministischer Zustandsautomat

Nicht empfohlen:

- Präfixauflösung über lineare Stringvergleiche
- Sonderbehandlung einzelner historischer Familien wie `Ctrl-K`
- Recycling des letzten ungültigen Tokens nach Fehlversuch

---

## 19. Zug 4 ausgearbeitet: Kontext-Resolver

Dieser Zug legt fest, wie aus dem tatsächlichen UI-Zustand genau **ein** aktiver Keymap-Kontext entsteht.

Die Kernentscheidung lautet:

- keine Mengenlogik gleichzeitig aktiver Kontexte
- kein opportunistisches Durchprobieren mehrerer Kontexte
- pro Tastenevent genau **ein** aufgelöster Laufzeitkontext

### 19.1 Rohfakten versus aufgelöster Kontext

Der Resolver arbeitet bewusst zweistufig:

1. Er sammelt rohe UI-Fakten.
2. Er reduziert diese Fakten deterministisch auf genau einen Keymap-Kontext.

Rohe UI-Fakten können etwa sein:

- Menü aktiv
- modaler Dialog aktiv
- fokussiertes Control ist eine Liste
- fokussiertes Control ist ein Eingabefeld
- aktives Fenster ist Editor
- aktiver Editor ist readonly

Wichtige Festlegung:

- Diese Rohfakten sind interne Resolver-Eingaben.
- Serialisiert, gebunden und verglichen wird **nicht** gegen Rohfakten, sondern nur gegen den aufgelösten Keymap-Kontext.

### 19.2 Aufgelöste Kontexte in Phase 1

Für Phase 1 werden diese Kontexte fest definiert:

- `MENU`
- `DIALOG`
- `DIALOG_LIST`
- `LIST`
- `READONLY`
- `EDIT`

Fachliche Begründung:

- `EDIT` und `READONLY` dürfen nicht derselbe Kontext sein.
- `LIST` außerhalb eines Dialogs ist semantisch nicht dasselbe wie eine Liste innerhalb eines modalen Dialogs.
- `MENU` ist ein eigener Interaktionsmodus und darf nicht implizit unter `DIALOG` oder `LIST` versteckt werden.

### 19.3 Bedeutung der Kontexte

`MENU`

- aktiver Menübaum, Popup-Menü oder Menüleiste
- Navigation und Aktivierung folgen der Menülogik, nicht der Editorlogik

`DIALOG`

- modaler Dialog ist aktiv
- Fokus liegt **nicht** auf einer Listen-Komponente
- typische Fälle: Buttons, Radio-Cluster, Checkboxes, Input-Felder, Slider

`DIALOG_LIST`

- modaler Dialog ist aktiv
- Fokus liegt auf einer Listen-Komponente oder historienartigen Liste im Dialog
- Trennung von `DIALOG` ist bewusst, weil Listenbedienung andere Keyfamilien braucht als Formular-Controls

`LIST`

- nicht-modale oder nicht-dialogische Listen-/Browser-/Manager-Ansicht
- Beispiele später: Dateilisten, Trefferlisten, Browserartige Ansichten

`READONLY`

- editorartige Textansicht ohne Schreibrecht
- Hilfeviewer oder readonly Editoransicht fallen hier hinein

`EDIT`

- normaler schreibbarer Editor

### 19.4 Resolver-Regel

Die Auflösung erfolgt in genau dieser Reihenfolge:

1. `MENU`
2. `DIALOG_LIST`
3. `DIALOG`
4. `LIST`
5. `READONLY`
6. `EDIT`

Das ist keine heuristische Präferenz, sondern die verbindliche Resolver-Reihenfolge.

Bedeutung:

- Ist ein Menü aktiv, gewinnt immer `MENU`.
- Ist ein modaler Dialog aktiv und der Fokus liegt auf einer Liste, gewinnt `DIALOG_LIST`.
- Ist ein modaler Dialog aktiv und der Fokus liegt nicht auf einer Liste, gewinnt `DIALOG`.
- Außerhalb modaler Dialoge kann eine Listenansicht `LIST` liefern.
- Erst wenn keine der UI-Hüllen greift, wird zwischen `READONLY` und `EDIT` unterschieden.

### 19.5 Warum `READONLY` vor `EDIT` liegt

`READONLY` ist kein kosmetisches Attribut, sondern semantisch ein anderer Interaktionsmodus.

Folgen:

- Schreibkommandos dürfen dort anders gebunden sein oder ganz fehlen.
- Cursor- und Suchbefehle können identisch bleiben, müssen es aber nicht.
- Hilfe- oder Viewer-Modi dürfen damit einen eigenen Bindungssatz tragen, ohne eine zweite Spezialarchitektur zu brauchen.

### 19.6 Keine Kontextmengen im Binding-Modell

Ein Binding verweist in Phase 1 auf genau **einen** Kontext.

Nicht zulässig:

- `context=EDIT|READONLY`
- `context=DIALOG,*`
- implizite Gruppen wie "gilt überall außer Menü"

Begründung:

- Solche Mengenangaben machen Konfliktprüfung und Erklärbarkeit unnötig schwer.
- Für Phase 1 ist Eindeutigkeit wichtiger als Komfortsyntax.

Empfehlung:

- Wenn dieselbe Sequenz in mehreren Kontexten dieselbe Funktion auslösen soll, werden mehrere explizite Bindings angelegt.

### 19.7 Keine implizite Kontextvererbung in Phase 1

Wird in einem Kontext kein Binding gefunden, dann gilt zunächst:

- kein automatischer Fallback auf einen "ähnlichen" Kontext
- kein stiller Rückgriff von `DIALOG_LIST` auf `DIALOG`
- kein stiller Rückgriff von `READONLY` auf `EDIT`

Begründung:

- Implizite Vererbung klingt bequem, verschleiert aber Konflikte und erschwert Diagnose.
- Ein deterministischer Resolver ist wenig wert, wenn danach doch wieder weich durch mehrere Ebenen gefallen wird.

Spätere Erweiterung denkbar:

- ein expliziter `COMMON`- oder `GLOBAL`-Kontext

Aber:

- nicht in Phase 1

### 19.8 Verhalten bei unbekanntem oder nicht auflösbarem UI-Zustand

Wenn kein definierter Kontext aufgelöst werden kann, gilt:

- der Keymap-Resolver liefert `NONE`
- aus der Keymap wird nichts dispatcht
- die bestehende UI kann den Tastenschritt regulär verarbeiten

Begründung:

- Ein unsicher geratener Kontext darf nicht raten.
- Kein Dispatch ist fachlich sauberer als falscher Dispatch.

### 19.9 Beispiele

Beispiel A: normaler Editor

- aktives Fenster ist Editor
- Editor ist schreibbar
- kein Menü aktiv
- kein Dialog aktiv
- Ergebnis: `EDIT`

Beispiel B: Hilfefenster

- aktives Fenster ist editorartige Textansicht
- Ansicht ist readonly
- Ergebnis: `READONLY`

Beispiel C: Setup-Dialog mit Input-Feld

- modaler Dialog aktiv
- Fokus auf Eingabefeld
- Ergebnis: `DIALOG`

Beispiel D: Setup-Dialog mit History-Liste

- modaler Dialog aktiv
- Fokus auf Listen-Control
- Ergebnis: `DIALOG_LIST`

Beispiel E: Menüleiste offen

- Menü aktiv
- Ergebnis: `MENU`

Beispiel F: Dateiliste außerhalb eines Dialogs

- kein Menü
- kein modaler Dialog
- aktive Ansicht ist Listen-/Browseransicht
- Ergebnis: `LIST`

### 19.10 Auswirkung auf Pakete 1 bis 4

Für die ersten vier Pakete sind primär relevant:

- `EDIT`
- `READONLY`

Sekundär relevant:

- `LIST`
- `DIALOG`
- `DIALOG_LIST`

Fachlich wichtig:

- Cursorbewegung und Delete-Familien werden in `EDIT` und `READONLY` voraussichtlich früh gebraucht.
- `DIALOG` und `DIALOG_LIST` müssen trotzdem jetzt schon im Modell stehen, weil sonst spätere Dialog- und Listenbedienung wieder mit Sonderpfaden nachgerüstet werden muss.

### 19.11 Empfehlung für die Implementierung

Meine Empfehlung ist:

- Resolver als kleine zentrale Routine
- Eingabe sind rohe UI-Fakten
- Ausgabe ist ein `enum class` mit genau einem Kontext oder `NONE`

Nicht empfohlen:

- Bindings gegen rohe Widgettypen
- mehrfaches Lookup in mehreren Kontexten "bis etwas passt"
- Vererbungskaskaden schon in Phase 1

---

## 20. Zug 5 ausgearbeitet: Zweischichtiges Aktionsmodell

Dieser Zug zieht die Trennung zwischen kanonischem MRMAC-Codex und echten MR-Erweiterungen formal fest.

Die Leitentscheidung lautet:

- `MRMAC_*` und `MR_*` sind keine kosmetischen Präfixe
- sie bezeichnen zwei verschiedene Herkunftsschichten
- diese Herkunft bleibt im Modell, in der Validierung und in der UI sichtbar

### 20.1 Die zwei Aktionsschichten

Schicht 1: `MRMAC_*`

- referenzgebundener kanonischer Codex
- geschlossen in dem Sinn, dass neue IDs nicht opportunistisch hineinerfunden werden
- dient als historisch und funktional definierter Grundbestand

Schicht 2: `MR_*`

- echte MR-spezifische Erweiterungen
- offen für Funktionen, die MR fachlich braucht, die aber nicht Teil des MRMAC-Codex sind
- ausdrücklich **nicht** als "moderne Umbenennung" von MRMAC-Funktionen gedacht

### 20.2 Warum die Trennung nötig ist

Ohne diese Trennung entstünden sofort drei Probleme:

- MR-Sonderfunktionen würden still als kanonisch getarnt
- die Referenztreue zum MRMAC-Codex ginge verloren
- Hilfe, Key Manager und spätere Dokumentation könnten nicht mehr sauber anzeigen, worauf sich ein Binding eigentlich bezieht

Fachlich klar gesagt:

- Ein gemeinsamer unmarkierter Namensraum wäre bequem, aber architektonisch schlampig.

### 20.3 Kanonische Aktions-IDs

Bindings referenzieren künftig **vollqualifizierte** Aktions-IDs.

Beispiele:

- `MRMAC_FILE_SAVE`
- `MRMAC_CURSOR_UP`
- `MRMAC_BLOCK_DELETE`
- `MR_SEARCH_RESULTS_NEXT`
- `MR_WORKSPACE_RELOAD`

Wichtige Festlegungen:

- Es gibt keine zweite Kurzform ohne Präfix.
- `FILE_SAVE` allein ist keine kanonische Ziel-ID.
- `MRMAC_FILE_SAVE` und `MR_FILE_SAVE` wären zwei verschiedene Aktionen und dürfen nicht als Synonyme behandelt werden.

### 20.4 Bedeutung von `MRMAC_*`

`MRMAC_*` bedeutet:

- die Aktion ist Teil des referenzgebundenen Codex
- ihre Semantik ist stabil
- Profile dürfen sie binden, aber nicht umdefinieren

Zulässig:

- mehrere Sequenzen auf dieselbe `MRMAC_*`-Funktion
- verschiedene Sequenzen je Kontext

Unzulässig:

- eine Profildefinition, die behauptet, `MRMAC_BLOCK_DELETE` bedeute nun etwas anderes
- eine heimliche MR-Variante unter MRMAC-Namen

### 20.5 Bedeutung von `MR_*`

`MR_*` bedeutet:

- die Aktion ist eine echte MR-Erweiterung
- sie ist nicht Teil des kanonischen MRMAC-Bestands
- sie darf in denselben Profilmechanismus eingebunden werden, bleibt aber als MR-spezifisch sichtbar

Typische Kandidaten wären später:

- MR-spezifische Workspace-Funktionen
- MR-spezifische Dialog- oder Managerfunktionen
- MR-spezifische Komfortfunktionen ohne historisches Pendant

Wichtige Festlegung:

- `MR_*` ist kein Abfalleimer für unsauber verstandene MRMAC-Funktionen.
- Erst wenn fachlich klar ist, dass eine Funktion **nicht** im Codex liegt, gehört sie in `MR_*`.

### 20.6 Validierungsregeln

Für Aktionsziele gilt:

- Eine Binding-Ziel-ID muss entweder `MRMAC_*`, `MR_*` oder ein Makroziel sein.
- Eine unbekannte Aktions-ID ist invalid.
- Eine Ziel-ID darf genau eine Semantik haben.
- Zwischen `MRMAC_*` und `MR_*` gibt es keine Alias-Beziehung.

Beispiele:

- gültig:
  `target=MRMAC_CURSOR_UP` bei `type=Action`
- gültig:
  `target=MR_SEARCH_RESULTS_NEXT` bei `type=Action`
- invalid:
  `target=CURSOR_UP` bei `type=Action`
- invalid:
  `target=MRMAC_CURSOR_UP` bei `type=Action`, wenn diese ID im Katalog nicht existiert

### 20.7 Sichtbarkeit in UI und Dokumentation

Die Herkunft der Aktion muss sichtbar bleiben in:

- Key Manager
- Hilfe
- Menü-/Statusableitungen
- Validierungs- und Fehlermeldungen

Mindesterwartung:

- eine Aktion kann als `MRMAC` oder `MR` erkannt werden

Empfehlung:

- sichtbare Herkunftsspalte oder Kennzeichnung im Key Manager
- keine UI, die alle Aktionsarten zu einer unmarkierten Flachliste zusammenstaucht

### 20.8 Beziehung zum zentralen Aktionskatalog

Es soll einen zentralen Aktionskatalog geben, der für beide Schichten autoritativ ist.

Jeder Eintrag trägt mindestens:

- Aktions-ID
- Herkunftsschicht: `MRMAC` oder `MR`
- kanonischen Anzeigenamen
- kurze Beschreibung
- optionale Kontext-Hinweise

Wichtige Festlegung:

- Menü, Hilfe, Statuszeile und Key Manager lesen diese Aktionsdefinitionen aus demselben Katalog.
- Damit bleibt die Leitlinie gewahrt: eine Modellquelle, mehrere Ansichten.

### 20.9 Verhältnis zu Makrozielen

Makros sind **keine** dritte Aktionsschicht.

Stattdessen gilt:

- `MRMAC_*` und `MR_*` sind katalogisierte Aktionen
- Makros sind referenzielle Ziele eigener Art

Deshalb darf ein Binding-Ziel nur genau eines sein:

- Aktion aus `MRMAC_*`
- Aktion aus `MR_*`
- Makro

Keine Mischform.

### 20.10 Auswirkung auf Serialisierung

Für `KEYMAP_BIND` bedeutet das:

- das Feld `type=` beschreibt die Zielart
- das Feld `target=` enthält die vollqualifizierte Aktions-ID oder Makro-ID
- Kurzformen ohne Herkunftspräfix sind nicht kanonisch

Beispiel:

```text
MRSETUP('KEYMAP_BIND', 'name="WORDSTAR" context=EDIT type=Action target="MRMAC_FILE_SAVE" sequence="<Ctrl+K> <Ctrl+S>" description="Save file"');
MRSETUP('KEYMAP_BIND', 'name="DEFAULT" context=LIST type=Action target="MR_SEARCH_RESULTS_NEXT" sequence="<F6>" description="Next result"');
```

### 20.11 Kritische Empfehlung

Meine Empfehlung ist klar:

- `MRMAC_*` strikt geschlossen halten
- `MR_*` als explizite Erweiterungsschicht führen
- keinerlei Synonymisierung zwischen beiden Welten zulassen

Nicht empfohlen:

- nackte IDs wie `FILE_SAVE`
- stilles "Aufwerten" von MR-Funktionen zu MRMAC
- parallele Bezeichner für dieselbe Aktion in verschiedenen Schichten

---

## 21. Zug 6 ausgearbeitet: Dateischnitt für `~/mr/keymap`

Dieser Zug zieht den geplanten Dateischnitt für das neue Top-Level-Modul `~/mr/keymap` fachlich fest.

Leitgedanke:

- keine Sammeldatei für "alles rund um Keymapping"
- stattdessen kleine Module mit klarer Verantwortung
- keine Vermischung von Datenmodell, Parser, Resolver und UI-Kopplung

### 21.1 Zielstruktur

Vorläufiger Dateischnitt:

- `keymap/MRKeymapActionCatalog.hpp`
- `keymap/MRKeymapActionCatalog.cpp`
- `keymap/MRKeymapToken.hpp`
- `keymap/MRKeymapToken.cpp`
- `keymap/MRKeymapSequence.hpp`
- `keymap/MRKeymapSequence.cpp`
- `keymap/MRKeymapContext.hpp`
- `keymap/MRKeymapContext.cpp`
- `keymap/MRKeymapProfile.hpp`
- `keymap/MRKeymapProfile.cpp`
- `keymap/MRKeymapTrie.hpp`
- `keymap/MRKeymapTrie.cpp`
- `keymap/MRKeymapResolver.hpp`
- `keymap/MRKeymapResolver.cpp`
- `keymap/MRKeymapCapture.hpp`
- `keymap/MRKeymapCapture.cpp`

Wichtige Änderung gegenüber der früheren Grobliste:

- Aus dem früher allgemein benannten Resolver-Baustein wird zusätzlich explizit `MRKeymapTrie.*`.
- Das ist fachlich sauberer, weil Trie-Struktur und Laufzeit-Resolver nicht dieselbe Verantwortung haben.

### 21.2 Verantwortlichkeiten der Module

`MRKeymapActionCatalog.*`

- autoritativer Katalog aller `MRMAC_*`- und `MR_*`-Aktionen
- Lookup nach Aktions-ID
- Herkunftsschicht, Anzeigename, Kurzbeschreibung, optionale Kontext-Hinweise

Nicht zuständig für:

- Tasten-Parsing
- Profil-Serialisierung
- Laufzeit-Pending-State

`MRKeymapToken.*`

- Modell eines einzelnen Key-Tokens
- Modifier, Basis-Key, Normalisierung eines Einzel-Tokens
- String-Parsing und String-Serialisierung eines Einzel-Tokens

Nicht zuständig für:

- Sequenzlogik
- Präfixauflösung

`MRKeymapSequence.*`

- Modell einer geordneten Tokenfolge
- Sequenz-Normalisierung
- Vergleich, Hashing, Serialisierung ganzer Sequenzen

Nicht zuständig für:

- Kontextwahl
- Trie-Aufbau

`MRKeymapContext.*`

- Definition der aufgelösten Laufzeitkontexte
- Resolver-Eingabefakten und Reduktionsregeln

Fachliche Warnung:

- Hier gehört nur Kontextmodell hinein, nicht der gesamte Dispatch.

`MRKeymapProfile.*`

- strukturierte Sicht auf ein Keymap-Profil aus dem autoritativen K/V-Hash
- Profil-ID, Metadaten, Binding-Records, Validierung auf Profilebene
- Projektion zwischen Hash-Records und Profilsicht

Nicht zuständig für:

- UI-Dialoge
- Key-Capture
- tatsächliche Tastendispatch-Laufzeit

`MRKeymapTrie.*`

- Trie-Knotenstruktur für Sequenzen pro Kontext
- Aufbau aus validierten Bindings
- Konflikterkennung auf Struktur-Ebene

Nicht zuständig für:

- aktive UI-Zustände
- Messageline-Ausgabe
- Kontextwahl

`MRKeymapResolver.*`

- Laufzeitauflösung eines Tastenevents
- Pending-State
- Abort-Verhalten
- harter Reject bei ungültiger Fortsetzung
- Dispatch-Vorentscheidung: Treffer, Pending, Invalid, None

Wichtige Festlegung:

- Der Resolver konsumiert Trie und Kontext, er definiert sie nicht.

`MRKeymapCapture.*`

- zentrale Programmlogik für Key-Capture
- Konsumierbar für Key Manager und Macro Manager
- Liefert kanonische Token-/Sequenzobjekte statt UI-spezifischer Strings

Nicht zuständig für:

- Profilverwaltung
- Laufzeit-Dispatch

### 21.3 Abhängigkeitsrichtung

Die Modulabhängigkeiten sollen bewusst einseitig sein:

- `MRKeymapActionCatalog` ist basal
- `MRKeymapToken` ist basal
- `MRKeymapSequence` hängt von `MRKeymapToken` ab
- `MRKeymapContext` ist basal
- `MRKeymapProfile` hängt von `MRKeymapSequence`, `MRKeymapContext` und `MRKeymapActionCatalog` ab
- `MRKeymapTrie` hängt von `MRKeymapProfile` und `MRKeymapSequence` ab
- `MRKeymapResolver` hängt von `MRKeymapTrie` und `MRKeymapContext` ab
- `MRKeymapCapture` hängt von `MRKeymapToken` und `MRKeymapSequence` ab

Nicht gewünscht sind Zyklen wie:

- Resolver <-> Profile
- Capture <-> Resolver
- ActionCatalog <-> Profile

### 21.4 Was bewusst **nicht** in `~/mr/keymap` gehört

Nicht in dieses Modul gehören:

- Dialogklassen
- konkrete TVision-Views
- Menüimplementierungen
- direkte `MREditor`-Sonderlogik
- Screen-Ausgabe oder Messageline-Rendering

Begründung:

- `keymap` soll Fachlogik kapseln, nicht UI-Spezialfälle sammeln.
- Die UI konsumiert dieses Modul, aber dieses Modul kennt die UI nur über schmale Eingabefakten.

### 21.5 Schnittstelle zur UI

Die UI-Schicht liefert an `keymap` nur:

- rohe Key-Events
- rohe UI-Fakten für den Kontext-Resolver
- gegebenenfalls eine Zielumgebung für Dispatch-Ergebnisse

`keymap` liefert zurück:

- aufgelösten Kontext
- Resolver-Ergebnis: `Matched`, `Pending`, `Aborted`, `Invalid`, `NoMatch`
- referenzierte Aktions-ID oder Makroziel

Wichtige Festlegung:

- Die UI fragt das Keymap-System.
- Das Keymap-System ruft nicht selbst an beliebigen Stellen in UI-Komponenten hinein.

### 21.6 Profil- und Laufzeitdaten trennen

Es gibt zwei deutlich getrennte Datenebenen:

1. Profil-Daten
   Das ist die strukturierte Sicht auf die editierbaren Hash-Daten.
2. Laufzeit-Daten
   Das ist die aus Profilen abgeleitete, effiziente Struktur für Dispatch.

Profil-Daten liegen in:

- `MRKeymapProfile.*`

Laufzeit-Daten liegen in:

- `MRKeymapTrie.*`
- `MRKeymapResolver.*`

Begründung:

- Hash-basierte Editierbarkeit und Laufzeiteffizienz sind verschiedene Probleme.
- Ein einziges "Allzweckobjekt" würde beides schlecht lösen.

### 21.7 Behandlung des `DEFAULT`-Profils

Die bereits notierte `DEFAULT`-Idee wirkt auf den Dateischnitt:

- `DEFAULT` ist ein normales Profilobjekt in `MRKeymapProfile.*`
- der Nicht-Löschbar-Aspekt ist **kein** Profilmodellthema
- diese Restriktion gehört später in Profilverwaltung bzw. Dialoglogik, nicht in das Basisdatenmodell

Das ist wichtig:

- Sonst würde das Dateimodell schon früh UI-Policy in seine Kernobjekte hineinziehen.

### 21.8 Empfehlung zur Implementierungsreihenfolge der Module

Meine Empfehlung:

1. `MRKeymapActionCatalog.*`
2. `MRKeymapToken.*`
3. `MRKeymapSequence.*`
4. `MRKeymapContext.*`
5. `MRKeymapProfile.*`
6. `MRKeymapTrie.*`
7. `MRKeymapResolver.*`
8. `MRKeymapCapture.*`

Begründung:

- erst stabiles Vokabular
- dann serialisierbare Struktur
- dann Laufzeitstruktur
- Capture erst danach, weil Capture ohne Kanonform leicht in UI-Sonderlogik abrutscht

### 21.9 Kritische Empfehlung

Ich empfehle ausdrücklich:

- `MRKeymapTrie.*` als eigenes Modul
- `MRKeymapCapture.*` strikt ohne Dialogcode
- `MRKeymapProfile.*` nur für das editierbare Modell

Nicht empfohlen:

- ein monolithisches `MRKeymap.cpp`
- Zusammenwurf von Trie und Resolver
- Profilobjekte, die zugleich Pending-State oder UI-Flags tragen

---

## 22. Zug 7 ausgearbeitet: Wahrheitsquelle und Trie-Kapselung

Dieser Zug zieht die Architekturregel fest, die für das gesamte Keymapping-System unverhandelbar sein soll:

- der zentrale K/V-Hash ist die einzige Wahrheit
- der Trie ist nur ein gekapselter Laufzeitindex

### 22.1 Einzige Wahrheit

Für Keymapping gilt:

- `settings.mrmac` ist die persistierte Darstellung
- der zentrale K/V-Hash ist die geladene, editierbare Wahrheit
- alle strukturierten Sichten werden daraus abgeleitet

Folgerungen:

- Es gibt keine zweite autoritative Registry im Profilobjekt.
- Es gibt keine zweite autoritative Registry im Trie.
- Es gibt keinen Write-Pfad, der den Trie ändert, ohne den Hash zu ändern.
- Es gibt keinen Serialisierungspfad aus dem Trie.

Fachlich klar gesagt:

- Wenn Hash und Trie widersprüchlich sein können, ist das ein Bug und kein zulässiger Zustand.

### 22.2 Rolle von `MRKeymapProfile.*`

`MRKeymapProfile.*` ist:

- eine strukturierte Sicht auf Hash-Daten
- ein Arbeitsmodell für Validierung und UI-nahe Bearbeitung

`MRKeymapProfile.*` ist **nicht**:

- ein zweiter Master-Datenspeicher
- ein alternativer Persistenzort
- eine unabhängige Registry neben dem K/V-Hash

### 22.3 Rolle von `MRKeymapTrie.*`

`MRKeymapTrie.*` ist:

- ein aus der autoritativen Quelle abgeleiteter Laufzeitindex
- ephemer
- jederzeit vollständig neu aufbaubar

`MRKeymapTrie.*` ist **nicht**:

- editierbar als Fachmodell
- serialisierbar
- von außen inspizierbare Registry

### 22.4 Rebuild-only-Regel

Für den Trie gilt die harte Regel:

- bei relevanter Änderung des Keymapping-Bestands wird der Trie verworfen
- danach wird er neu aufgebaut
- es gibt kein in-place Patchen einzelner Trie-Knoten als öffentlichen Pfad

Begründung:

- Das verhindert Schleichzustände.
- Das hält den Write-Pfad eindeutig beim K/V-Hash.
- Das vermeidet inkrementelle Reparaturlogik als Fehlerquelle.

### 22.5 Trie als stark gekapselte Klasse

Die Trie-Struktur wird in eine Klasse gekapselt, deren Datenbestand vollständig verborgen ist.

Verbindliche Kapselungsregel:

- Knoten
- Children-Relationen
- Sequenzablagen
- Zielreferenzen
- interne Zustandsfelder

sind ausschließlich `private`.

Es gibt keine publizierten Methoden:

- zum Iterieren über Knoten
- zum Lesen von Children-Tabellen
- zum direkten Lesen gespeicherter Aktionen
- zum partiellen Editieren des Datenbestands

### 22.6 Öffentliche Trie-API

Die öffentliche API von `MRKeymapTrie` bleibt minimal.

Erlaubt sind fachlich nur:

- Aufbau/Rebuild aus der autoritativen Quelle
- Resolve-/Akzeptanzentscheidung für Laufzeitevents

Empfohlene Form:

- `rebuildFrom...(...)`
- `resolve(...)`

Optional:

- `clear()`

Aber nur, wenn dies für den Lebenszyklus tatsächlich gebraucht wird.

### 22.7 Warum `bool` nicht reicht

Die Resolve-Methode soll **nicht** nur `true/false` liefern.

Begründung:

- Präfixfamilien brauchen mindestens den Zustand `Pending`
- harte Verwerfung braucht einen unterscheidbaren Fehlzustand
- Treffer, Pending und NoMatch sind fachlich verschiedene Ergebnisse

Empfohlene Rückgabeklasse:

- `Matched`
- `Pending`
- `Invalid`
- `Aborted`
- `NoMatch`

Mit optionaler Nutzlast:

- referenzierte Aktions-ID oder Makroziel bei `Matched`

### 22.8 Konsequenz für Diagnose und Validierung

Wenn Trie-Interna nicht publiziert werden, dann müssen Build und Resolve saubere Ergebnisobjekte liefern.

Deshalb gilt:

- Build meldet Konflikte und Strukturfehler über definierte Diagnosen
- Resolve meldet Laufzeitergebnisse über definierte Result-Typen
- Diagnose erfolgt nicht über Fremdcode, der in Trie-Interna herumsucht

Das ist fachlich stärker als öffentliche Getter:

- bessere Kapselung
- klarere Verantwortlichkeiten
- kein späteres API-Leck

### 22.9 Empfehlung

Meine Empfehlung lautet daher:

- `MRKeymapTrie` als vollständig gekapselte Klasse
- nur Rebuild und Resolve öffentlich
- alle Datenfelder `private`
- keine publizierten Strukturgetter
- K/V-Hash bleibt alleiniger Ort der Wahrheit

Nicht empfohlen:

- Trie als offen durchsuchbare Registry
- öffentliche Knotenzugriffe
- öffentliche Teilmutation des Trie
- Serialisierung oder Dirty-State am Trie

---

## 23. Zug 8 ausgearbeitet: Basis-APIs auf Papier

Dieser Zug zieht die ersten konkreten Schnittstellen so weit fest, dass daraus später Header-Dateien ohne erneute Grundsatzdiskussion ableitbar sein sollen.

Wichtig:

- Das ist noch **kein** Coding-Zug.
- Es ist eine API-Festlegung auf Planungsebene.
- Ziel ist minimale, belastbare Oberfläche statt früh ausufernder Klassen.

### 23.1 `MRKeymapActionCatalog`

Zweck:

- autoritative Definition aller bekannten `MRMAC_*`- und `MR_*`-Aktionen

Empfohlene Basistypen:

```cpp
enum class MRKeymapActionOrigin {
    MRMAC,
    MR
};

struct MRKeymapActionDefinition {
    std::string id;
    MRKeymapActionOrigin origin;
    std::string displayName;
    std::string description;
};
```

Empfohlene öffentliche API:

```cpp
class MRKeymapActionCatalog {
public:
    [[nodiscard]] const MRKeymapActionDefinition* findById(std::string_view id) const noexcept;
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
};
```

Wichtige Festlegungen:

- Kein schreibender Public-API-Pfad.
- Der Katalog ist statisch bzw. intern aufgebaut und danach nur lesbar.
- Kein zweiter Lookup über Kurznamen ohne Präfix.

### 23.2 `MRKeymapToken`

Zweck:

- Modell eines einzelnen kanonischen Key-Tokens

Empfohlene Basistypen:

```cpp
enum class MRKeymapModifier : std::uint8_t {
    Ctrl = 1 << 0,
    Alt = 1 << 1,
    Shift = 1 << 2
};

enum class MRKeymapBaseKey {
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    Enter, Tab, Esc, Backspace, Insert, Delete,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    KeypadPlus, KeypadMinus, KeypadMultiply, KeypadEnter,
    MouseUp, MouseDown, MouseLeft, MouseRight,
    MouseButtonLeft, MouseButtonRight, MouseButtonMiddle
};

class MRKeymapToken {
public:
    [[nodiscard]] static std::optional<MRKeymapToken> parse(std::string_view text);
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] MRKeymapBaseKey baseKey() const noexcept;
    [[nodiscard]] std::uint8_t modifiers() const noexcept;
    [[nodiscard]] bool hasModifier(MRKeymapModifier modifier) const noexcept;
};
```

Wichtige Festlegungen:

- `parse(...)` liefert bereits die kanonische Form oder `std::nullopt`.
- `toString()` liefert immer die eindeutige MR-Kanonform.
- Gleichheit und Hashing sollen strukturell sein, nicht stringbasiert.

### 23.3 `MRKeymapSequence`

Zweck:

- Modell einer geordneten Folge kanonischer Tokens

Empfohlene Form:

```cpp
class MRKeymapSequence {
public:
    [[nodiscard]] static std::optional<MRKeymapSequence> parse(std::string_view text);
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] std::span<const MRKeymapToken> tokens() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
};
```

Wichtige Festlegungen:

- Eine leere Sequenz ist fachlich ungültig.
- `tokens()` bleibt read-only.
- `parse(...)` normalisiert sofort in die Kanonform.

### 23.4 Binding-Zieltyp

Weil ein Binding entweder eine Aktion oder ein Makro referenziert, braucht es einen expliziten Zieltyp.

Empfohlene Form:

```cpp
enum class MRKeymapBindingTargetKind {
    Action,
    Macro
};

struct MRKeymapBindingTarget {
    MRKeymapBindingTargetKind kind;
    std::string id;
};
```

Wichtige Festlegungen:

- Bei `Action` ist `id` eine vollqualifizierte ID wie `MRMAC_CURSOR_UP`.
- Bei `Macro` ist `id` eine Makroreferenz.
- Keine Mischform, keine parallelen Aktion/Makro-Felder.

### 23.5 `resolve(...)`-Resultat

Für Trie und Resolver reicht kein `bool`.

Empfohlene Form:

```cpp
enum class MRKeymapResolveKind {
    Matched,
    Pending,
    Aborted,
    Invalid,
    NoMatch
};

struct MRKeymapResolveResult {
    MRKeymapResolveKind kind;
    std::optional<MRKeymapBindingTarget> target;
};
```

Bedeutung:

- `Matched`: vollständiger Treffer, `target` gesetzt
- `Pending`: gültiger Präfixzustand, `target` leer
- `Aborted`: Präfixzustand explizit abgebrochen, `target` leer
- `Invalid`: ungültige Fortsetzung einer begonnenen Sequenz, `target` leer
- `NoMatch`: kein Binding am `root`, `target` leer

Wichtige Festlegung:

- `Invalid` und `NoMatch` sind fachlich verschieden.
- Genau diese Trennung wird später für Messageline und Event-Fallthrough gebraucht.

### 23.6 `MRKeymapTrie`

Für die Trie-Klasse bleibt die API minimal.

Empfohlene Form:

```cpp
class MRKeymapTrie {
public:
    struct BuildIssue;
    struct BuildResult;

    [[nodiscard]] BuildResult rebuildFromBindings(/* abgeleitete Binding-Sicht */);
    [[nodiscard]] MRKeymapResolveResult resolve(/* Kontext, Event, Abort-Regel */);
    void clear() noexcept;
};
```

Wichtige Festlegungen:

- `BuildResult` liefert Diagnose nach außen, nicht Trie-Struktur.
- `resolve(...)` liefert nur das Resultat, nicht interne Knotenzustände.
- Alle Trie-Daten bleiben `private`.

### 23.7 `MRKeymapResolver`

Weil Kontextauflösung und Trie-Auflösung unterschiedliche Probleme lösen, bleibt `MRKeymapResolver` als koordinierende Laufzeitklasse sinnvoll.

Empfohlene Form:

```cpp
class MRKeymapResolver {
public:
    [[nodiscard]] MRKeymapResolveResult resolve(/* rohe UI-Fakten, Key-Event */);
    void rebuild() noexcept;
    void clearPending() noexcept;
};
```

Rolle:

- holt den aktiven Keymap-Kontext
- reicht das Event an den passenden Trie weiter
- verwaltet Pending-State auf Laufzeitebene

### 23.8 Kritische Empfehlung

Meine Empfehlung für die spätere konkrete Header-Arbeit ist:

- kleine value-artige Typen für `Token`, `Sequence`, `ResolveResult`
- minimalistische Klassenoberflächen
- keine APIs, die das Datenmodell nach außen aufbrechen

Nicht empfohlen:

- string-zentrierte APIs als Primärmodell
- Resolver-APIs mit vielen optionalen Parametern
- Trie- oder Profil-Getter, die interne Container leaken


## 24. Arbeitsentwurf: WordStar-Grundausstattung

Dieser Abschnitt hält einen **arbeitsfähigen Startbestand** für das spätere `WORDSTAR`-Profil fest. Er ist bewusst **nicht** der vollständige historische WordStar-Umfang, sondern eine priorisierte Grundausstattung für MR.

Wichtige Festlegung:

- Notation ab hier in der kanonischen MR-Schreibweise
- Aktionsnamen nach Möglichkeit als `MRMAC_*`
- offene oder neue Fälle werden ausdrücklich als `MR_*`-Kandidat, Makro-Kandidat oder Klärfall markiert

### 24.1 Direkt brauchbare Kernmenge

Cursor / Navigation:

- `<Ctrl+E>` -> `MRMAC_CURSOR_UP`
- `<Ctrl+X>` -> `MRMAC_CURSOR_DOWN`
- `<Ctrl+S>` -> `MRMAC_CURSOR_LEFT`
- `<Ctrl+D>` -> `MRMAC_CURSOR_RIGHT`
- `<Ctrl+Q> <Ctrl+R>` -> `MRMAC_CURSOR_TOP_OF_FILE`
- `<Ctrl+Q> <Ctrl+C>` -> `MRMAC_CURSOR_BOTTOM_OF_FILE`
- `<Ctrl+Q> <Ctrl+S>` -> `MRMAC_CURSOR_HOME`
- `<Ctrl+Q> <Ctrl+D>` -> `MRMAC_CURSOR_END_OF_LINE`
- `<Ctrl+Q> <Ctrl+E>` -> `MRMAC_CURSOR_TOP_OF_WINDOW`
- `<Ctrl+Q> <Ctrl+X>` -> `MRMAC_CURSOR_BOTTOM_OF_WINDOW`
- `<Ctrl+Q> <Ctrl+G>` -> `MRMAC_CURSOR_GOTO_LINE`

Marker / Position:

- `<Ctrl+L> <Digit1>` ... `<Ctrl+L> <Digit9>` -> `MRMAC_MARK_GET_RANDOM_ACCESS` mit Marker `1` ... `9`
- `<Ctrl+K> <Digit1>` ... `<Ctrl+K> <Digit9>` -> `MRMAC_MARK_SET_RANDOM_ACCESS` mit Marker `1` ... `9`

Datei / Block / Edit:

- `<Ctrl+K> <Ctrl+C>` -> `MRMAC_BLOCK_COPY`
- `<Ctrl+K> <Ctrl+Y>` -> `MRMAC_BLOCK_DELETE`
- `<Ctrl+K> <Ctrl+S>` -> `MRMAC_FILE_SAVE`
- `<Ctrl+C>` -> `MRMAC_VIEW_CENTER_LINE`

### 24.2 Brauchbar, aber noch fachlich zu klären

Diese Einträge sind sinnvoll, aber noch nicht direkt auf den bereits festgezogenen Codex abbildbar:

- `<Ctrl+L>` -> `last cursor position`
  Bewertung:
  Das riecht nach Positionsstack, ist aber noch nicht scharf genug. Kandidat wäre `MRMAC_MARK_POP_POSITION`, falls die gewünschte Semantik wirklich "zur letzten gemerkten Position springen" ist.

- `<Ctrl+K> <Ctrl+T>` -> `mark word right`
  Bewertung:
  Das passt eher zur bereits notierten Familie `MRMAC_BLOCK_EXTEND_BY_MOTION` mit einer Wort-Rechts-Bewegung als zu einer isolierten Einzelaktion.

- `<Ctrl+K> <Ctrl+B>` -> `block begin`
- `<Ctrl+K> <Ctrl+K>` -> `block end`
- `<Ctrl+K> <Ctrl+H>` -> `end marking`
- `<Ctrl+K> <Ctrl+A>` -> `begin column block`
  Bewertung:
  Diese vier Einträge verwenden WordStar-Semantik für Blockanfang/-ende, während der bisherige MRMAC-Codex eher Blocktypen und Blockoperationen beschreibt. Hier braucht es noch eine saubere Übersetzung zwischen WordStar-Bedienmodell und kanonischem Blockmodell.

- `<Ctrl+K> <Ctrl+M>` -> `block move`
  Bewertung:
  Fachlich sinnvoll, aber noch auf die kanonische Ziel-ID zurückzuführen. Erwartbarer Kandidat ist `MRMAC_BLOCK_MOVE`.

- `<Ctrl+K> <Ctrl+W>` -> `save block to file`
- `<Ctrl+K> <Ctrl+R>` -> `insert block from file`
  Bewertung:
  Das sind brauchbare Wünsche, aber im bisher festgezogenen Paket-4-Codex noch nicht sauber als Datei-gebundene Block-I/O-Aktionen modelliert.

### 24.3 MR-Kandidaten oder Makro-Kandidaten

Diese Wünsche sind als Startideen brauchbar, gehören aber nicht blind in `MRMAC_*`:

- `<Ctrl+K> <Ctrl+D>` -> `forced save without overwrite warning`
  Bewertung:
  Eher `MR_*` als `MRMAC_*`. Das ist eine MR-spezifische Policy-Funktion, kein kanonischer Grundbefehl.

- `<Ctrl+Q> <!>` -> `insert timestamp`
  Bewertung:
  Plausibler Makro-Kandidat oder später `MR_*`, aber kein guter Anlass, den MRMAC-Codex aufzublähen.

- `<Ctrl+B>` -> `block justify / wordwrap in column block`
  Bewertung:
  Eher Makro-Kandidat oder späterer `MR_*`-Befehl.

- `<Ctrl+W>` -> `toggle word wrap`
  Bewertung:
  Sehr wahrscheinlich `MR_*`, weil hier eine Editor-Policy bzw. ein Zustand getoggelt wird und nicht bloß eine klassische MRMAC-Grundfunktion ausgelöst wird.

### 24.4 Echte Konflikte im Entwurf

Der Entwurf ist fachlich brauchbar, aber **nicht konfliktfrei**:

- `<Ctrl+U>` ist doppelt belegt:
  - einmal für `unindent block`
  - einmal für `undo`
  Das kann im selben Kontext nicht gleichzeitig gelten.

- `<Ctrl+S>` ist doppelt belegt:
  - einmal für `MRMAC_CURSOR_LEFT`
  - einmal für `sort column block`
  Auch das ist im selben Kontext unzulässig, solange kein zusätzlicher Kontext oder Modus eingeführt wird.

Meine Empfehlung:

- `Undo` auf `<Ctrl+U>` ist als Kernfunktion deutlich wichtiger als `unindent block`.
- `sort column block` sollte nicht auf dem globalen Navigationstoken `<Ctrl+S>` liegen.

### 24.5 Terminal-kritische Stellen

Mehrere gewünschte WordStar-Bindings sind auf Terminalebene heikel und dürfen nicht naiv behandelt werden:

- `<Ctrl+I>` entspricht in ASCII praktisch `Tab`
- `<Ctrl+H>` entspricht häufig `Backspace`
- `<Ctrl+J>` entspricht `LF`
- `<Ctrl+M>` entspricht `Enter` bzw. `CR`

Folgen:

- `<Ctrl+I>` als neuer eigener Befehl für `indent block` ist in einem Terminalmodell fachlich riskant.
- `<Ctrl+K> <Ctrl+H>` und `<Ctrl+K> <Ctrl+M>` müssen in der Capture- und Tokenlogik bewusst behandelt werden, sonst kippen sie in `Backspace` bzw. `Enter`.
- Genau diese Fälle bestätigen, dass unser Tokenmodell nicht nur hübsch, sondern technisch notwendig ist.

Meine Empfehlung:

- `<Ctrl+I>` **nicht** als neue Grundbelegung für `indent block`
- die Sequenzen mit `<Ctrl+H>` und `<Ctrl+M>` erst dann festzurren, wenn die Terminalnormalisierung explizit dokumentiert ist
- `goto line` im portablen `WORDSTAR`-Profil von `<Ctrl+J>` auf `<Ctrl+Q> <Ctrl+G>` verlagern

### 24.6 Empfohlener Startbestand für `WORDSTAR`

Wenn ich Ihren Entwurf auf einen fachlich tragfähigen Startkern reduziere, dann ist meine Empfehlung:

1. Cursor / Navigation
   `<Ctrl+E>`, `<Ctrl+X>`, `<Ctrl+S>`, `<Ctrl+D>`, `<Ctrl+Q> <Ctrl+R>`, `<Ctrl+Q> <Ctrl+C>`, `<Ctrl+Q> <Ctrl+S>`, `<Ctrl+Q> <Ctrl+D>`, `<Ctrl+Q> <Ctrl+E>`, `<Ctrl+Q> <Ctrl+X>`, `<Ctrl+Q> <Ctrl+G>`
2. Marker
   `<Ctrl+L> <Digit1>` ... `<Ctrl+L> <Digit9>`, `<Ctrl+K> <Digit1>` ... `<Ctrl+K> <Digit9>`
3. Blockgrundlagen
   `<Ctrl+K> <Ctrl+C>`, `<Ctrl+K> <Ctrl+Y>`, plus später die geklärte Begin/End-Markierungsfamilie
4. Datei-Grundfunktion
   `<Ctrl+K> <Ctrl+S>`
5. On-screen Kern
   `<Ctrl+C>` für `MRMAC_VIEW_CENTER_LINE`

Fachliche Schlussfolgerung:

- Der Entwurf ist brauchbar.
- Er war in Rohform aber noch zu nah an historischer Notation und zu weit weg von unserem jetzigen Katalog.
- In der jetzigen Form kann er als Arbeitsbasis für das spätere `WORDSTAR`-Profil dienen, ohne dass wir uns bereits in unaufgelöste Sonderfälle hineinserialisieren.

---

## 25. Zug 9 ausgearbeitet: Profil-, Binding- und Diagnosemodell

Dieser Zug legt das editierbare Profilmodell so fest, dass daraus später:

- K/V-Hash-Projektion
- Dialogbearbeitung
- Validierung
- Trie-Rebuild

ohne Architekturbruch abgeleitet werden können.

Wichtige Leitlinie:

- Das Profilmodell ist eine strukturierte Sicht.
- Es ist nicht die Wahrheit selbst.
- Es ist aber die Arbeitsform, auf der Validierung und Dialogbearbeitung fachlich sauber stattfinden.

### 25.1 `MRKeymapProfile`

Zweck:

- strukturierte Sicht auf genau ein Keymap-Profil aus dem zentralen K/V-Hash

Empfohlene Form:

```cpp
struct MRKeymapProfile {
    std::string name;
    std::string description;
    std::vector<MRKeymapBindingRecord> bindings;
};
```

Wichtige Festlegungen:

- `name` ist die fachliche Profil-ID.
- `bindings` enthält nur die zu diesem Profil gehörenden Binding-Records.
- Der Nicht-Löschbar-Aspekt von `DEFAULT` gehört nicht in diese Struktur.

Nicht empfohlen:

- UI-Flags wie `selected`, `dirty`, `expanded`
- Laufzeitfelder wie Pending-State
- Verweise auf Trie-Knoten

### 25.2 `MRKeymapBindingRecord`

Zweck:

- fachliche Beschreibung genau eines Bindings

Empfohlene Form:

```cpp
struct MRKeymapBindingRecord {
    std::string profileName;
    MRKeymapContext context;
    MRKeymapBindingTarget target;
    MRKeymapSequence sequence;
    std::string description;
};
```

Wichtige Festlegungen:

- Ein Binding referenziert genau einen Kontext.
- Ein Binding referenziert genau ein Ziel.
- Ein Binding referenziert genau eine kanonische Sequenz.
- `description` ist optionaler Beschreibungstext und nicht Teil der Identität.

Empfohlene fachliche Identität:

- `profileName`
- `context`
- `target`
- `sequence`

### 25.3 Identität und Duplikate

Es müssen zwei Fälle klar getrennt werden:

1. echtes Duplikat
   Dieselben Identitätsfelder erscheinen mehrfach.
2. Konflikt
   Dieselbe Sequenz zeigt im selben Profil und Kontext auf verschiedene Ziele.

Festlegung:

- Echte Duplikate dürfen beim erneuten Serialisieren zusammenfallen.
- Konflikte dürfen **nicht** still zusammenfallen.
- Konflikte müssen als Diagnose sichtbar werden.

Beispiele:

- Duplikat:
  `WORDSTAR | EDIT | <Ctrl+E> | MRMAC_CURSOR_UP`
  zweimal identisch

- Konflikt:
  `WORDSTAR | EDIT | <Ctrl+E> | MRMAC_CURSOR_UP`
  und
  `WORDSTAR | EDIT | <Ctrl+E> | MRMAC_DELETE_LINE`

### 25.4 `KEYMAP_PROFILE`- und `KEYMAP_BIND`-Hash-Sicht

Die K/V-Wahrheit liegt in Record-Form vor.

Fachlich gibt es zwei Record-Arten:

- `KEYMAP_PROFILE`
- `KEYMAP_BIND`

Empfohlene minimale Feldsicht:

`KEYMAP_PROFILE`

- `name`
- `description`

`KEYMAP_BIND`

- `name`
- `context`
- `type`
- `target`
- `sequence`
- `description`

Wichtige Festlegung:

- `type` und `target` sind sauberer als ein mehrdeutiges Sammelfeld.
- Falls die aktuelle `MRSETUP(...)`-Serialisierung später lieber `function=` oder `macro=` getrennt schreibt, ist das ein Serialisierungsdetail.
- Im strukturierten Modell bleibt das Ziel trotzdem ein expliziter Variantentyp.

### 25.4.1 Kanonische Member-Trennung im zweiten `MRSETUP`-Parameter

Für strukturierte `MRSETUP`-Payloads wird die Trennung an der bestehenden Workspace-Serialisierung ausgerichtet.

Verbindliche Richtung:

- `key=value`-Member werden durch genau ein ASCII-Space getrennt
- Kommata werden **nicht** als allgemeiner Member-Trenner verwendet
- Kommata bleiben nur innerhalb zusammengesetzter Einzelwerte zulässig

Beispiel aus der bestehenden Workspace-Serialisierung:

```text
MRSETUP('WORKSPACE', 'URL=/tmp/foo.txt size=80,25 pos=1,1 cursor=10,5 vd=0');
```

Fachliche Folgerung für Keymap-Records:

- `name="WORDSTAR" context=EDIT type=Action target="MRMAC_FILE_SAVE"`
  ist die richtige Member-Trennung
- nicht:
  `name=WORDSTAR, context=EDIT, type=Action, target=MRMAC_FILE_SAVE`

Die Regel wird nun absichtlich verschärft:

- alle Stringwerte werden grundsätzlich in doppelte Anführungszeichen gesetzt
- Zahlen, Bool-Werte und andere atomare Nicht-String-Werte bleiben unquoted
- `sequence` wird immer gequotet
- `description` wird immer gequotet
- `name`, `target`, `file`, `URL` und sonstige stringartige Werte werden ebenfalls immer gequotet

Begründung:

- Linux-Pfade, Dateinamen und URLs dürfen Leerzeichen enthalten
- eine allgemeine Quote-Regel ist robuster als feldweise Sonderbehandlung
- Serializer und Parser bleiben dadurch einfacher und konsistenter

Konsequenz:

- Die bestehende Workspace-Serialisierung sollte perspektivisch ebenfalls auf gequotete Stringwerte umgestellt werden
- Beispiel künftig eher:
  `MRSETUP('WORKSPACE', 'URL="/tmp/My Project/foo.txt" size=80,25 pos=1,1 cursor=10,5 vd=0');`

Beispiele:

```text
MRSETUP('KEYMAP_PROFILE', 'name="DEFAULT" description="Built-in default key profile"');
MRSETUP('KEYMAP_BIND', 'name="WORDSTAR" context=EDIT type=Action target="MRMAC_FILE_SAVE" sequence="<Ctrl+K> <Ctrl+S>" description="Save file"');
MRSETUP('KEYMAP_BIND', 'name="WORDSTAR" context=EDIT type=Macro target="insert_timestamp" sequence="<Ctrl+Q> <!>" description="Insert timestamp"');
```

### 25.5 Projektion Hash -> Profilsicht

Die Projektion vom zentralen K/V-Hash in `MRKeymapProfile` erfolgt in diesen Schritten:

1. Alle `KEYMAP_PROFILE`-Records einlesen.
2. Für jeden Profilnamen ein `MRKeymapProfile` anlegen.
3. Alle `KEYMAP_BIND`-Records einlesen.
4. Jeden Binding-Record dem Profil `name` zuordnen.
5. Unbekannte Profile als Diagnose markieren.
6. Sequenzen, Kontexte und Ziele in ihre kanonischen Strukturtypen parsen.

Wichtige Festlegungen:

- Die Projektion darf fehlerhafte Records diagnostizieren, ohne den ganzen Bestand sofort unbrauchbar zu machen.
- Das Ergebnis ist daher nicht nur `profiles`, sondern `profiles + diagnostics`.

### 25.6 Projektion Profilsicht -> Hash

Die Rückprojektion in den K/V-Hash folgt der umgekehrten Richtung:

1. Bestehende `KEYMAP_PROFILE`- und `KEYMAP_BIND`-Records im Hash-Bereich ersetzen.
2. Strukturierte Profile in kanonische Records serialisieren.
3. Nur die K/V-Daten gelten danach als editierte Wahrheit.

Wichtige Festlegungen:

- Die Profilsicht schreibt nie direkt in den Trie.
- Nach Hash-Update wird der Trie verworfen und neu aufgebaut.
- Serialisiert wird nur die kanonische Form.

### 25.7 Diagnosetypen

Für Build und Validierung braucht es eine explizite Diagnosestruktur.

Empfohlene Form:

```cpp
enum class MRKeymapDiagnosticKind {
    UnknownProfile,
    DuplicateProfile,
    DuplicateBinding,
    ConflictingBinding,
    UnknownContext,
    InvalidSequence,
    UnknownAction,
    InvalidMacroTarget,
    PrefixConflict,
    TerminalPrefixConflict
};

enum class MRKeymapDiagnosticSeverity {
    Warning,
    Error
};

struct MRKeymapDiagnostic {
    MRKeymapDiagnosticKind kind;
    MRKeymapDiagnosticSeverity severity;
    std::string message;
};
```

Wichtige Festlegungen:

- Diagnose ist ein strukturierter Befund, kein bloßer String.
- `message` ist für Anzeige und Log hilfreich, aber nicht die einzige Informationsebene.

### 25.8 Welche Diagnosen hart blockieren

Nicht jede Diagnose muss das ganze Profil unbrauchbar machen.

Meine Empfehlung:

Harte Fehler für Trie-Rebuild:

- `UnknownContext`
- `InvalidSequence`
- `UnknownAction`
- `PrefixConflict`
- `TerminalPrefixConflict`
- `ConflictingBinding`

Weiche Fehler / weiter ladbar:

- `DuplicateBinding`
- `InvalidMacroTarget`
- `UnknownProfile` nur dann, wenn der Record isoliert ist und nicht sinnvoll zuordenbar bleibt

Fachliche Präzisierung:

- "weiter ladbar" bedeutet nicht "still ignoriert".
- Solche Fälle bleiben im Key Manager sichtbar.

### 25.9 `DEFAULT` im Profilmodell

`DEFAULT` ist im Profilmodell ein normales Profil:

- `name=DEFAULT`
- frei editierbar
- normal validierbar

Nicht Teil des Profilmodells sind:

- Löschschutz
- UI-Sonderdarstellung
- Fallback-Policy

Diese Punkte gehören später in:

- Profilverwaltung
- aktive Profilumschaltung
- Dialoglogik

### 25.10 Kritische Empfehlung

Meine Empfehlung ist:

- Profilobjekt klein halten
- Binding-Identität sauber definieren
- Diagnosen als strukturierte Typen modellieren
- Hash-Projektion und Rückprojektion explizit festhalten

Nicht empfohlen:

- Profilbearbeitung direkt auf Rohstrings
- stilles Heilen echter Konflikte
- Profilobjekte mit UI- oder Trie-Zustand zu vermischen

---

## 26. Zug 10 ausgearbeitet: Payload-Grammatik und Escaping

Nach der Entscheidung für grundsätzlich gequotete Stringwerte muss die Payload-Grammatik des zweiten `MRSETUP`-Parameters explizit festgezogen werden.

Wichtige Leitlinie:

- eine Grammatik
- ein Serializer
- ein Parser
- keine feldspezifischen Sonderwelten

### 26.1 Grammatik des zweiten `MRSETUP`-Parameters

Der zweite `MRSETUP`-Parameter bleibt ein einzelnes mrmac-Stringliteral. Sein Inhalt folgt einer eigenen `key=value`-Payload-Grammatik.

Informelle Grammatik:

```text
payload      := member (SP member)*
member       := key "=" value
key          := [A-Za-z][A-Za-z0-9_]*
value        := quoted-string | atom
atom         := [^ "\t\r\n]+
quoted-string:= '"' char* '"'
char         := escaped-char | normal-char
escaped-char := '\' '"' | '\' '\' | '\' 'n' | '\' 'r' | '\' 't'
```

Fachliche Präzisierung:

- Member werden durch genau ein ASCII-Space getrennt.
- Ein `atom` darf keine Leerzeichen enthalten.
- Stringwerte werden kanonisch grundsätzlich als `quoted-string` serialisiert.
- `atom` bleibt nur für numerische, boolesche oder sonstige nicht-stringartige Werte vorgesehen.

### 26.2 Zwei Escape-Ebenen

Hier existieren bewusst zwei verschiedene Ebenen:

1. Innere Payload-Ebene
   Diese betrifft den Inhalt von `"..."` innerhalb der Payload.
2. Äußere mrmac-Literal-Ebene
   Diese betrifft das umgebende Single-Quote-Literal von `MRSETUP(...)`.

Das ist wichtig, weil dieselbe Textstelle sonst leicht doppelt oder falsch escaped wird.

### 26.3 Escaping auf der inneren Payload-Ebene

Innerhalb eines gequoteten Stringwerts gelten kanonisch diese Escapes:

- `\"` für doppeltes Anführungszeichen
- `\\` für Backslash
- `\n` für Newline
- `\r` für Carriage Return
- `\t` für Tab

Empfehlung:

- Die Escape-Menge klein halten.
- Nicht unnötig C-artige Vollständigkeit vorspielen.

Nicht vorgesehen als Pflichtumfang:

- hex-Escapes
- octal-Escapes
- Unicode-Escapes

### 26.4 Escaping auf der äußeren mrmac-Ebene

Der gesamte Payload-Text wird weiterhin als Single-Quote-Literal an `MRSETUP(...)` übergeben.

Folge:

- Ein einfaches Hochkomma innerhalb der Payload muss nach den mrmac-Regeln escaped werden.
- Die innere Payload-Grammatik ersetzt **nicht** die Regeln des äußeren mrmac-Literals.

Fachliche Konsequenz für den Serializer:

- zuerst den Payload-Text in seiner inneren Form erzeugen
- danach diesen Gesamtpayload für das äußere mrmac-Single-Quote-Literal escapen

Das ist die einzig saubere Reihenfolge.

### 26.5 Kanonische Serialisierungsregeln

Für Keymap-Records gelten damit diese festen Regeln:

- Member-Reihenfolge ist stabil
- Member-Trennung ist genau ein ASCII-Space
- alle Stringwerte werden gequotet
- innere Stringwerte werden mit den kanonischen Payload-Escapes serialisiert
- der gesamte Payload wird anschließend für das äußere mrmac-Literal escaped

Beispiel `KEYMAP_BIND`:

```text
MRSETUP('KEYMAP_BIND', 'name="WORDSTAR" context=EDIT type=Action target="MRMAC_FILE_SAVE" sequence="<Ctrl+K> <Ctrl+S>" description="Save file"');
```

Beispiel mit Leerzeichen und Hochkomma im Dateinamen:

```text
MRSETUP('ACTIVE_KEYMAP_PROFILE', 'name="DEFAULT" file="/home/idoc/Mr Raus'' Profile.mrmac"');
```

Wichtiger Punkt:

- Das doppelte `'` im Beispiel gehört zur äußeren mrmac-Literal-Ebene.
- Die inneren `"` gehören zur Payload-Ebene.

### 26.6 Parser-Regeln

Der Parser für den zweiten `MRSETUP`-Parameter soll:

- den bereits entpackten Payload-Text konsumieren
- also **nach** der mrmac-Literal-Entschlüsselung arbeiten

Dann gilt:

- Der Payload-Parser sieht `name="DEFAULT"`, nicht mehr die Single-Quote-Ebene.
- Der Payload-Parser ist nur für `key=value` plus innere Quotes/Escapes zuständig.

Das trennt die Zuständigkeiten sauber:

- mrmac-Parser verarbeitet `MRSETUP('...', '...')`
- Keymap-Payload-Parser verarbeitet den Inhalt des zweiten Parameters

### 26.7 Fehlerfälle

Folgende Fälle müssen als Parse-/Serialisierungsfehler behandelt werden:

- doppelter Schlüssel im selben Payload
- fehlendes `=`
- ungeschlossene `"`-Quote
- unbekanntes Escape wie `\q`
- unquoted Stringwert, wo der Serializer immer Quotes verlangt
- führender oder mehrfacher Member-Whitespace in kanonischer Ausgabe

Fachliche Präzisierung:

- Der Parser darf aus Kompatibilitätsgründen später toleranter sein.
- Der Serializer bleibt trotzdem hart kanonisch.

### 26.8 Empfehlung

Meine Empfehlung lautet:

- Payload-Grammatik klein und streng halten
- zwei Escape-Ebenen explizit trennen
- Serializer immer kanonisch, Parser optional leicht toleranter

Nicht empfohlen:

- Mischformen aus unquoted und quoted Stringwerten nach Belieben
- direkte Stringkonkatenation ohne formales Escaping-Modell
- Parserlogik, die gleichzeitig mrmac-Literal und Payload-Grammatik behandelt

---

## 27. Zug 11 ausgearbeitet: Terminal-/TTY-Normalisierung

Dieser Zug legt fest, wie das Keymapping-System auf Linux-Terminals mit kontrollastigen ASCII-Kollisionen umgehen soll.

Der Befund aus der tatsächlichen Eingabeschicht ist eindeutig genug, dass hier eine harte Planungsregel sinnvoll ist.

### 27.1 Technischer Befund aus TVision / MR

TVision kennt zwar symbolisch eigene Konstanten wie:

- `kbCtrlH`
- `kbCtrlI`
- `kbCtrlJ`
- `kbCtrlM`

aber auf Terminal-Backends werden mehrere dieser Fälle bereits sehr früh auf Standardtasten zusammengezogen.

Relevante Beobachtungen:

- In `tvision/source/platform/ncursinp.cpp` wird
  - `^H` als `kbBack`
  - `^I` als `kbTab`
  - `^J` als `kbEnter`
  - `^M` als `kbEnter`
  behandelt.
- In `tvision/source/platform/termio.cpp` werden
  - Codepoint `8` zu `kbBack`
  - Codepoint `9` zu `kbTab`
  - Codepoint `13` zu `kbEnter`
  normalisiert.
- MR selbst behandelt `kbCtrlI` bereits heute tab-verwandt, etwa in Dialog- und Editorpfaden.
- TVision selbst mappt historisch
  - `kbCtrlH` auf Backspace-Semantik
  - `kbCtrlM` auf Newline-Semantik

Fachliche Schlussfolgerung:

- Auf Linux-TTY ist die Unterscheidung zwischen bestimmten `Ctrl`-Buchstaben und `Backspace` / `Tab` / `Enter` nicht belastbar.

### 27.2 Kanonische Äquivalenzklassen für Linux-TTY

Für TTY-orientierte Eingabe gilt in Phase 1 diese Normalisierungsrichtung:

- `Backspace`-Klasse:
  - `kbBack`
  - `kbCtrlH`
  - terminalabhängig auch `DEL`/`127`

- `Tab`-Klasse:
  - `kbTab`
  - `kbCtrlI`

- `Enter`-Klasse:
  - `kbEnter`
  - `kbCtrlM`
  - auf curses/ncurses faktisch auch `kbCtrlJ`

Wichtige Festlegung:

- Diese Äquivalenzklassen gelten für die portable Terminalsicht.
- Das Keymapping-System soll dort nicht so tun, als könne es zuverlässig zwischen diesen Varianten unterscheiden, wenn die Eingabeschicht das bereits zerstört hat.

### 27.3 Konsequenz für kanonische Tokens

Für die kanonische MR-Tokenform bedeutet das:

- `<Backspace>` bleibt kanonisch
- `<Tab>` bleibt kanonisch
- `<Enter>` bleibt kanonisch

Aber:

- `<Ctrl+H>`
- `<Ctrl+I>`
- `<Ctrl+M>`

sind auf Linux-TTY **nicht** als verlässlich eigenständige portable Tokens zu behandeln.

Für `<Ctrl+J>` gilt:

- auf Linux-TTY ebenfalls nicht belastbar genug für Phase 1
- insbesondere auf curses/ncurses kollidiert es faktisch mit `Enter`

### 27.4 Harte Portabilitätsregel für Phase 1

Für terminalportable Keymap-Profile gilt:

- Bindings, die die Unterscheidung zwischen
  - `<Backspace>` und `<Ctrl+H>`
  - `<Tab>` und `<Ctrl+I>`
  - `<Enter>` und `<Ctrl+M>`
  - `<Enter>` und `<Ctrl+J>`
  voraussetzen, sind nicht zulässig.

Das ist eine harte Regel, keine Warnung zweiter Klasse.

Begründung:

- Die Unterscheidung ist auf dem Zielsystem nicht verlässlich garantierbar.
- Ein Profil, das darauf beruht, wäre auf Linux-TTY fachlich unehrlich.

### 27.5 Auswirkung auf den WordStar-Entwurf

Diese Regel trifft unmittelbar einige bereits diskutierte Wünsche:

- `<Ctrl+I>` für `indent block`
  - nicht portabel
  - in Phase 1 nicht als TTY-sicherer Standard

- `<Ctrl+K> <Ctrl+H>` für `end marking`
  - problematisch, weil der zweite Tastenschritt mit `Backspace` kollidiert

- `<Ctrl+K> <Ctrl+M>` für `block move`
  - problematisch, weil der zweite Tastenschritt mit `Enter` kollidiert

- `<Ctrl+J>` für `goto line`
  - problematisch, weil es auf TTY nicht verlässlich von `Enter` unterscheidbar ist

Fachlich klar gesagt:

- Gerade `<Ctrl+J>` als WordStar-Kernbindung ist unter Linux-TTY der stärkste Problemfall.
- Wenn MR ernsthaft terminaltreu bleiben soll, darf diese Belegung nicht einfach nostalgisch übernommen werden.
- Für das portable `WORDSTAR`-Profil wird deshalb `<Ctrl+Q> <Ctrl+G>` als Ersatzbelegung für `goto line` vorgesehen.

### 27.6 Empfohlene Behandlung solcher Bindings

Option A: vollständig verbieten

- technisch am saubersten
- meine Empfehlung für Phase 1

Option B: als backend-abhängig markieren

- denkbar für eine spätere GUI-/Enhanced-Keyboard-Schicht
- aber für Phase 1 fachlich zu weich

Option C: zulassen, aber mit Warnung

- nicht empfohlen
- das würde dem Benutzer eine Funktion suggerieren, die auf Linux-TTY real nicht zuverlässig ist

Meine Empfehlung:

- Phase 1: solche Bindings für terminalportable Profile als invalid diagnostizieren
- erst später über backend-spezifische oder nicht-TTY-Kontexte nachdenken

### 27.7 Neue Diagnoserichtung

Aus dieser Regel folgt ein zusätzlicher Diagnosebedarf:

- `NonPortableTerminalBinding`

Bedeutung:

- Das Binding ist semantisch verständlich, aber auf Linux-TTY nicht belastbar unterscheidbar.

Empfohlene Einstufung:

- für terminalorientierte Profile: `Error`
- für spätere nicht-TTY-Kontexte denkbar als `Warning`

### 27.8 Konsequenz für den Parser und Capture

Parser und Capture dürfen nicht mehr versprechen als die Eingabeschicht liefern kann.

Deshalb gilt:

- Die Capture-Logik soll bei TTY-Eingabe keine künstlich präziseren Tokens vortäuschen.
- Wenn der Backend-Input bereits nur `Tab` liefert, darf der Key Manager daraus nicht `Ctrl+I` fantasieren.
- Die sichtbare Tokenanzeige muss die tatsächlich normalisierte Form zeigen.

### 27.9 Kritische Empfehlung

Meine Empfehlung ist daher eindeutig:

- Linux-TTY als Wahrheitsgrenze akzeptieren
- problematische `Ctrl`-ASCII-Kollisionen in Phase 1 nicht als unterscheidbar modellieren
- WordStar-Startprofil an dieser Stelle pragmatisch anpassen statt historisch zu romantisieren

Nicht empfohlen:

- künstliche Sonderpfade nur für nostalgische WordStar-Bindings
- UI-Anzeige von Tokens, die der Backend-Input real nicht zuverlässig liefern kann

---

## 28. Übergang zur Implementierung

Die Planungstiefe ist jetzt für einen ersten Coding-Slice ausreichend.

Meine Empfehlung für den Einstieg in echten Code ist:

1. `MRKeymapActionCatalog.*`
   Begründung:
   stabiler Wortschatz, keine UI-Abhängigkeit, geringe Risikooberfläche
2. `MRKeymapToken.*`
   Begründung:
   direkt aus der festgezogenen Kanonform ableitbar
3. `MRKeymapSequence.*`
   Begründung:
   schließt die Normalisierung und Serialisierung auf Objektebene

Noch **nicht** als erster Coding-Slice empfohlen:

- `MRKeymapTrie.*`
- `MRKeymapResolver.*`
- Dialog- oder Setup-Integration

Begründung:

- Erst muss das Vokabular und die Kanonisierung stabil im Code stehen.
- Sonst baut man Laufzeitlogik auf beweglichem Fundament.

Konkrete Minimalziele für den ersten Coding-Slice:

- Aktionskatalog mit `MRMAC_*`-Einträgen für Pakete 1 bis 4
- strukturierter `MRKeymapToken`
- Parser und Serializer für kanonische Tokens
- strukturierte `MRKeymapSequence`
- Parser und Serializer für Sequenzen

Fachliche Empfehlung:

- Danach sofort einmal integrierte Roundtrip-Proben fahren, bevor Trie oder Resolver begonnen werden.
- Also erst: `parse -> normalize -> serialize`
- dann: Katalog-Lookup
- erst danach: Präfix-Laufzeit
