# MR / MEMAC – Planungsnotizen zum Key-Mapping-System

## Zielsetzung

Für MR/MEMAC soll ein profilfähiges Key-Mapping-System geplant werden, das sich an den kanonischen MEMAC-Funktionen der Referenz orientiert. Es soll nicht nur WordStar-artige Belegungen unterstützen, sondern grundsätzlich auch andere Bedienmodelle wie Emacs oder Nano. Es muss möglich sein eine beliebig lange Folge von Steuerungstasten einer Funktion zuzuordnen: Beispiel Ctrl-K-V fpr Blockmove bei WordStar. Emacs kennt noch längere Steuerungs Tastenketten. 

Wesentliche Leitlinie:

> Die Funktionen müssen an genau einer Stelle modelliert, geladen, validiert und zur Laufzeit aufgelöst werden. Menü, Statuszeile, Hilfetext und Dialog sind nur Ansichten darauf.

---

## 1. Kanonischer Funktionskatalog

- Grundlage ist ausschließlich die MEMAC-Referenz.
- Diese Funktionen bilden den verbindlichen Codex.
- Erweiterungen werden nicht vorweggenommen.
- Profile dürfen diese Funktionen nur binden, aber nicht redefinieren.
- Die semantische Bedeutung von beispielsweise `FILE_SAVE`, `CURSOR_UP`, `BLOCK_DELETE` usw. ist zentral und starr.
- Keymapping-Profile definieren nur die Zuordnung von Tasten bzw. Präfixen auf diese kanonischen Funktionen oder auf Makros.

---

## 2. Profilmodell statt Einzelbelegung

Key-Mapping wird von Anfang an als **Profilproblem** modelliert.

Beabsichtigte Profile:

- WordStar
- Emacs
- Nano
- benutzerdefinierte Profile

`vi` wird zunächst bewusst verschoben.

Begründung:

- `vi` ist nicht nur ein anderes Mapping, sondern ein modaler Zustandsautomat.
- Das würde in dieser Phase unnötig viel Churn in Datenmodell, Event-Routing und Statuslogik erzeugen.
- Wenn Kontexte bereits sauber vorgesehen werden, kann `vi` später ohne architektonischen Bruch ergänzt werden.

---

## 3. Trennung der Ebenen

### 3.1 Kanonische Aktion

Die Codebase kennt feste kanonische MEMAC-Funktionen.

### 3.2 Key-Mapping-Profil

Ein Profil ordnet Aktionen pro Kontext konkrete Tasten oder Tastensequenzen zu.

### 3.3 Laufzeitkontext

Bindungen gelten nicht zwangsläufig überall gleich. Daher wird ein Kontextbegriff vorgesehen, etwa:

- `EDIT`
- `DIALOG`
- `READONLY`
- `LIST`

Spätere Erweiterungen bleiben möglich.

---

## 4. Serialisierung ausschließlich über `MRSETUP(...)`

Strategisch gesetzt ist:

- Es wird **keine neue Direktive** eingeführt.
- Die Serialisierung erfolgt in `settings.mrmac` über MRSETUP('ACTIVE_KEYMAP_PROFILE', 'name=WORDSTAR, file=/home/idoc/mr/mrmac/macros/wordstar.mrmac')
- Schlägt das laden fehl wird das Default Keymapping aktiviert und eine Error Nachricht im Log und auf der Messageline in Error Colorierung emittiert
- Key mapping profile dürfen analog zu Color Themes in mrmac Dateien gesaved und von dort geladen werden. Sie werden instantan angewendet

Beispielhafte Richtung:

```text
MRSETUP('KEYMAP_PROFILE', 'name=WORDSTAR, description=WordStar controls');
MRSETUP('KEYMAP_BIND', 'name=WORDSTAR, context=EDIT, function=FILE_SAVE,  sequence=[CTRL-K, CTRL-S], description=Save file');
MRSETUP('KEYMAP_BIND', 'name=WORDSTAR, context=EDIT, function=CURSOR_UP, sequence=[CTRL-E], description=Cursor Up');
```

Wesentlich ist:

- `KEYMAP_PROFILE` und `KEYMAP_BIND` dürfen mehrfach vorkommen.
- Diese Records werden beim Laden als **Liste** gesammelt.
- Der bootstrap baut einen Subhash im zentralen K/V Hash für die Keymappings auf. Die Erkennung geschieht über endliche Automaten
- Ein Binding ist inhaltlich identifizierbar, ohne dass seine Position in der Datei eine Rolle spielen muss.
- Validierung erkennt Konflikte explizit.
- Doppelte Bindungen (gleiche Sequenz, gleiche FUnktion) überschreiben silent, so dass die nächste Serialisierung von settings.mrmac die Dopplung beseitigt.
- Mehrere Auslöser für dieselbe Funktion sind ausdrücklich zulässig. Beispiel:
  - `BLOCK_BEGIN` auf `Ctrl-F7`
  - `BLOCK_BEGIN` auf `F12`
  Das ist **kein Konflikt**.


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

Wenn ein Binding auf ein Makro zeigt, muss dieses Makro existieren und ladbar sein. Ansonsten wird das Bindung entfernt und eine Errormeldung in Richtung Log und Messageline emittiert.

### Regel 5: Sequenzen werden vor Vergleichen kanonisch normalisiert

- Zur Definition von Bindungs wird ein neuer Dialog mit dem Subeintrag "Key mappings" ausgelöst. Als Orientierung hier ein Screenshot vom Original: documentation/pngsjpegs/Bildschirmfoto_20260408_204423.png
- Die Registrierung einer Funktion geschieht über die bereits implementierte "Record Macro" Routine. Im Dialog wird eine Liste der Funktionen zu ihren Key-Sequenzen verwaltet (Plural ist hier korrekt!). Sequenzen können gelöscht werden aber jede kanonische Funktion aus MEMAC muss in der Liste einmal vorkommen - auch wenn sie keine Sequenz zugewiesen bekommen hat.
- Der Satz der zu belegenden FUnktionen ist in documentation/pngsjpegs/Bildschirmfoto_20260426_121232.png definiert
- Nach dem recording einer Sequenz für eine Funktion ist die Sequenz in der Form aus der MEMAC Referenz darzustellen siehe documentation/pngsjpegs/Bildschirmfoto_20260426_121401.png


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

- Kanonischen MEMAC-Funktionskatalog aus der Referenz festziehen
- Keymapping-Profilmodell definieren
- Serialisierung ausschließlich über `MRSETUP(...)`
- Listen-Semantik für `KEYMAP_PROFILE` / `KEYMAP_BIND` sauber modellieren

### Phase 2

- aktives Profil laden und anwenden
- direkte Einzeltastenbindungen
- Makroziele im Profil erlauben
- Makrokatalogdateien mit mehreren Makros verwenden


## 12. Zusammenfassung

Das geplante System soll:

- auf einem festen MEMAC-Codex beruhen
- profilebasiert sein
- ausschließlich über `MRSETUP(...)` serialisieren
- semantisch reihenfolgeunabhängig laden
- Konflikte explizit validieren
- Mehrfachbindungen pro Funktion erlauben
- Präfixfamilien etwa CTRL-K für Wordstar Block Kommandos sauber unterstützen

