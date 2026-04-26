# MR / MEMAC – Planungsnotizen zum Key-Mapping-System
Unser letzter Stand war die experimentelle Modellierung der ASCII / Emoji Tabelle in mrmac. Auch wenn das Vorhaben nicht so gut gelungen ist wie die Implementierung des Calculators halte ich den Ausbau in Richtung Menü Manipulationen für wertvoll für mrmac. Ich möchte das Thema mrmac zunächst verlassen und mich mit Dir einem zentralen neuen Thema zuwenden : Key Mapping Themes.
Stand: 2026-04-16

## Zielsetzung

Für MR/MEMAC soll ein profilfähiges Key-Mapping-System geplant werden, das sich an den kanonischen MEMAC-Funktionen der Referenz orientiert. Es soll nicht nur WordStar-artige Belegungen unterstützen, sondern grundsätzlich auch andere Bedienmodelle wie Emacs oder Nano. `vi` wird in dieser Phase bewusst ausgeklammert.

Wesentliche Leitlinie:

> Die Funktionen müssen an genau einer Stelle modelliert, geladen, validiert und zur Laufzeit aufgelöst werden. Menü, Statuszeile, Hilfetext und Dialog sind nur Ansichten darauf.

---

## 1. Kanonischer Funktionskatalog

Es wird ein fester, kanonischer Satz von MEMAC-Funktionen definiert.

- Grundlage ist ausschließlich die MEMAC-Referenz.
- Diese Funktionen bilden den verbindlichen Codex.
- Erweiterungen werden nicht vorweggenommen.
- Profile dürfen diese Funktionen nur binden, aber nicht redefinieren.

Das bedeutet:

- Die semantische Bedeutung von `FILE_SAVE`, `CURSOR_UP`, `BLOCK_DELETE` usw. ist zentral und starr.
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

Ein Profil ordnet Aktionen pro Kontext konkrete Tasten oder Präfixe zu.

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
- Die Serialisierung erfolgt ausschließlich flach in `settings.mrmac` über `MRSETUP(...)`.

Das ist konsistent mit dem bereits bestehenden Muster für History-/Listen-artige Daten.

Wichtige Folgerung:

`MRSETUP` wird hier **nicht** als simples Key→Value-Modell verstanden, sondern als **Transporthülle für serialisierte Records**.

Beispielhafte Richtung:

```text
MRSETUP('ACTIVE_KEYMAP_PROFILE', 'WORDSTAR');
MRSETUP('KEYMAP_PROFILE', 'WORDSTAR|WordStar controls');
MRSETUP('KEYMAP_BIND', 'WORDSTAR|EDIT|FILE_SAVE|DISPATCHER_MACRO|CTRL-K||WordStarCtrlK');
MRSETUP('KEYMAP_BIND', 'WORDSTAR|EDIT|CURSOR_UP|DIRECT_ACTION|CTRL-E||CURSOR_UP');
```

Wesentlich ist:

- `KEYMAP_PROFILE` und `KEYMAP_BIND` dürfen mehrfach vorkommen.
- Diese Records werden beim Laden als **Liste** gesammelt.
- Sie dürfen **nicht** als normales Last-Wins-K/V missverstanden werden.

---

## 5. Warum die Reihenfolge in `settings.mrmac` für Keymaps nicht semantisch sein sollte

Im Gespräch wurde geklärt, dass History-Einträge heute absichtlich in einer Reihenfolge serialisiert werden, die fachliche Bedeutung trägt: jüngere Nutzung steht weiter oben.

Für Keymaps ist die Lage anders.

### 5.1 Warum Reihenfolge hier nicht fachlich nötig ist

Ein Keymap-Binding ist fachlich eindeutig beschreibbar durch Felder wie:

- Profil-ID
- Kontext
- Sequenz
- Zieltyp
- Ziel

Damit ist ein Binding inhaltlich identifizierbar, ohne dass seine Position in der Datei eine Rolle spielen muss.

### 5.2 Warum unsortierte Speicherung grundsätzlich möglich ist

Ja: **unsortierte Serialisierung ist grundsätzlich möglich**, solange beim Laden nicht auf Reihenfolge vertraut wird.

Dann gilt:

- Die Bedeutung eines Bindings ergibt sich aus seinen Feldern.
- Die Ladephase sammelt alle Bindings in Listen/Records.
- Validierung erkennt Konflikte explizit.
- Kein Binding „gewinnt“ nur deshalb, weil es später in der Datei steht.

### 5.3 Warum ich trotzdem eine stabile Schreibordnung empfehle

Eine stabile Ordnung ist nicht wegen Semantik wichtig, sondern wegen:

- Lesbarkeit
- Reproduzierbarkeit
- sauberer Diffbarkeit
- verständlicher Review- und Merge-Vorgänge

Das heißt:

- **Unsortiert ist fachlich zulässig.**
- **Stabil sortiert ist operativ angenehmer.**

Diese Sortierung sollte jedoch nur kosmetisch/organisatorisch wirken, nicht semantisch.

Empfehlung:

- intern semantisch reihenfolgeunabhängig arbeiten
- beim Schreiben optional stabil sortieren
- aber niemals „später in der Datei = höhere Priorität“ einführen

---

## 6. Mehrere Bindings auf dieselbe Funktion

Mehrere Auslöser für dieselbe Funktion sind ausdrücklich zulässig.

Beispiel:

- `BLOCK_BEGIN` auf `Ctrl-F7`
- `BLOCK_BEGIN` auf `F12`

Das ist **kein Konflikt**.

Deshalb wurde vorgeschlagen, intern **nicht** mit festen Feldern wie „Primary“ und „Alternate“ zu arbeiten, sondern:

- eine Funktion hat `0..n` Bindings
- jedes Binding ist ein eigener Record
- die UI darf später zwei davon als „Primary“ und „Alternate“ darstellen

Das ist flexibler und sauberer.

---

## 7. Mehrtastenfolgen und Präfixe

### 7.1 Grundsatz

Die Erkennung von Tastenfolgen gehört nicht in die VM als allgemeine Key-Speicherlogik.

### 7.2 Präfix-Dispatcher über Makros

Für WordStar-artige Familien kann sehr gut ein Präfix auf ein Dispatcher-Makro zeigen.

Beispiel:

- `Ctrl-K` startet ein MRMAC-Dispatcher-Makro
- das Makro ruft `READ_KEY`
- es wertet `KEY1`/`KEY2` aus
- danach ruft es die eigentliche kanonische Funktion auf

Das ist dank der MEMAC-Referenz (`READ_KEY`, `CHECK_KEY`) sachlich passend.

### 7.3 Reichweite dieser Idee

Dieses Modell eignet sich nicht nur für WordStar, sondern prinzipiell auch für andere Präfixfamilien, etwa Emacs-artige Sequenzen.

---

## 8. Makrokatalog statt Einzelfiles

Es wurde geprüft, ob `.mrmac`-Dateien mehrere Makros enthalten können.

Ergebnis:

**Ja, die aktuelle Codebase unterstützt Mehrfachmakros pro Datei bereits.**

Folgerung:

- Kein Zwang zu einer Datei pro Funktion.
- Sinnvoll ist ein Makrokatalog mit mehreren Makros pro Datei.
- Beispielhaft denkbar:
  - `wordstar-dispatchers.mrmac`
  - `editor-tools.mrmac`
  - `insert-tools.mrmac`

Das ist performanter und organisatorisch sauberer.

---

## 9. Validierungsregeln für Key-Mapping-Profile

Die Regeln sollen knapp, aber hart sein.

### Regel 1: Profil-ID muss eindeutig sein

Jedes Profil braucht eine eindeutige Profil-ID.

Unzulässig:

- dasselbe Profil zweimal definieren

### Regel 2: Jeder Binding-Record muss genau ein Ziel haben

Ein Binding darf genau eine Zielart besitzen:

- direkte kanonische Funktion
- Dispatcher-Makro
- direktes Makro

Keine Mischformen.

### Regel 3: Direkte Funktionen müssen aus dem MEMAC-Codex stammen

Direkte Bindings dürfen nur auf bekannte kanonische MEMAC-Funktionen zeigen.

Keine freien Fantasienamen, keine Tippfehler.

### Regel 4: Makroziele müssen referenziell auflösbar sein

Wenn ein Binding auf ein Makro zeigt, muss dieses Makro existieren und ladbar sein.

### Regel 5: Sequenzen werden vor Vergleichen kanonisch normalisiert

Unterschiedliche Schreibweisen derselben Tastenfolge dürfen intern nicht als verschieden behandelt werden.

Beispielhaft äquivalent:

- `Ctrl-K-S`
- `CTRL-K CTRL-S`
- `ctrl+k, ctrl+s`

### Regel 6: Derselbe Auslöser darf im selben Profil und Kontext nicht auf verschiedene Ziele zeigen

Schlüssel:

- `(profileId, context, normalizedSequence)`

Dieser Schlüssel darf nur ein Ziel haben.

Fehlerbeispiel:

- `WORDSTAR | EDIT | Ctrl-K Ctrl-B -> BLOCK_BEGIN`
- `WORDSTAR | EDIT | Ctrl-K Ctrl-B -> BLOCK_DELETE`

### Regel 7: Exakte Duplikate desselben Bindings sind redundant

Dasselbe Binding mehrfach zu serialisieren ist kein semantischer Konflikt, aber redundant.

Empfehlung:

- Warnung beim Validieren
- Deduplizierung beim Re-Write möglich

### Regel 8: Mehrere verschiedene Auslöser auf dieselbe Funktion sind zulässig

Beispiel:

- `Ctrl-F7 -> BLOCK_BEGIN`
- `F12 -> BLOCK_BEGIN`

Das ist erlaubt.

### Regel 9: Präfixüberschneidung ist nur über expliziten Dispatcher zulässig

Problematischer Fall:

- `Ctrl-K -> SHOW_HELP`
- `Ctrl-K Ctrl-S -> FILE_SAVE`

Dann ist `Ctrl-K` zugleich vollständige Aktion und Präfix.

Für die erste Ausbaustufe soll das nur in dieser Form zulässig sein:

- `Ctrl-K -> DISPATCHER_MACRO WordStarCtrlK`

Dann übernimmt das Makro die weitere Entscheidung.

### Regel 10: Präfixe zweier direkter Bindungen dürfen nicht konkurrieren

Beispiel:

- `Ctrl-X -> DIRECT_ACTION CutLine`
- `Ctrl-X Ctrl-S -> DIRECT_ACTION Save`

Das soll in Phase 1 nicht direkt nebeneinander erlaubt sein.

Stattdessen:

- `Ctrl-X -> DISPATCHER_MACRO EmacsCtrlX`

### Regel 11: Kontext trennt Bindings sauber

Dieselbe Taste darf in verschiedenen Kontexten unterschiedliche Funktionen haben.

Beispiel:

- `EDIT | F3 -> SEARCH`
- `DIALOG | F3 -> DELETE_HISTORY_ENTRY`

### Regel 12: Nur bekannte Kontexte sind zulässig

Für die erste Phase werden nur fest definierte Kontexte erlaubt.

### Regel 13: Leere oder syntaktisch unvollständige Sequenzen sind unzulässig

Beispiele:

- leere Sequenz
- nur Modifier
- kaputte Folgeform

### Regel 14: Dispatcher-Makros dürfen nicht ins Leere zeigen

Wenn ein Präfix an ein Dispatcher-Makro gebunden ist, muss dieses Makro existieren und ladbar sein.

### Regel 15: Dateireihenfolge darf keine Prioritätssemantik tragen

Selbst wenn `settings.mrmac` unsortiert geschrieben würde, darf die Reihenfolge niemals darüber entscheiden, welches Binding gilt.

Konflikte werden durch Validierung geklärt, nicht durch Zeilenreihenfolge.

---

## 10. Verdichtete Kernregeln

Wenn man die Regelmenge auf den harten Kern reduziert, bleiben diese Punkte:

1. Profil-ID eindeutig
2. Ziel eindeutig und referenziell gültig
3. `(Profil, Kontext, Sequenz)` darf nur ein Ziel haben
4. Präfixüberschneidung nur über expliziten Dispatcher
5. Mehrere Sequenzen auf dieselbe Funktion sind erlaubt

---

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

### Phase 3

- `READ_KEY`
- `CHECK_KEY`
- `KEY1`
- `KEY2`
- Dispatcher-Makros für Präfixfamilien wie WordStar oder Emacs

---

## 12. Zusammenfassung

Das geplante System soll:

- auf einem festen MEMAC-Codex beruhen
- profilebasiert sein
- ausschließlich über `MRSETUP(...)` serialisieren
- semantisch reihenfolgeunabhängig laden
- Konflikte explizit validieren
- Mehrfachbindungen pro Funktion erlauben
- Präfixfamilien über Dispatcher-Makros sauber unterstützen
- Makrokatalogdateien mit mehreren Makros nutzen

Damit ist der Grundriss so angelegt, dass WordStar, Emacs, Nano und benutzerdefinierte Profile sauber hineinpassen, ohne schon jetzt modale `vi`-Komplexität einzuschleppen.
