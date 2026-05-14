# Plan: Tree-sitter-freies Syntax-Highlighting für MR

## Ziel

MR erhält eine eigene, robuste Syntax-Pipeline:

```text
Textzeile + Eingangsstate
    -> kompakte Token-Runs
    -> Ausgangsstate
```

Der Editor rendert nur noch Token-Runs. Er kennt weder Tree-sitter noch lex/flex noch spätere Backend-Details.

Zentrale Eigenschaften:

- line-based
- zustandsbehaftet
- inkrementell
- cachefähig
- später parallelisierbar
- keine AST-Abhängigkeit im Rendering
- kein Parser im Draw-Hotpath

## Verbindliche Qualitätsregel

Neue Sprachunterstützung wird in MR nicht als halbfertige Baseline stehen gelassen.

Prinzip:

```text
Neue Sprache hinzufügen
    -> Lexer + Dateierkennung
    -> zustandsbehaftete Integration im Editorpfad
    -> Warmup/Checkpoint/Cache korrekt
    -> Goldstandard im bestehenden MR-Rahmen
```

Das bedeutet ausdrücklich:

- kein dauerhafter Zwischenzustand mit nur rudimentärer Colorierung
- keine neue Sprache ohne sofortige Fertigführung bis zum bestmöglichen Qualitätsstand im vorhandenen Architekturrahmen
- Mehrzeiler, State-Übergänge, Cache-Konvergenz und Sprungverhalten gehören zur Sprachfertigstellung dazu
- bei Lexer-Arbeit ist Code-Reuse ausdrücklich gewünscht; gemeinsame Scanner-, State-, Delimiter- und Helper-Logik soll bevorzugt wiederverwendet statt pro Sprache neu dupliziert werden

Wenn eine Sprache begonnen wird, wird sie in derselben Ausbaulinie bis zu diesem Qualitätsziel geführt, statt später auf eine unbestimmte Resteliste verschoben zu werden.

---

## Erreichte Teilziele

Erledigt:

- line-based Token-Run-Pipeline im Editorpfad aktiv
- Tree-sitter aus dem Rendering-Hotpath herausgelöst
- zustandsbehaftete Lexer-Integration mit Cache, Warmup und Checkpoints aktiv
- Ctrl-End-/EOF-Fehler behoben
- Ctrl-Home-/BOF-Sprungpfad an den robusten nicht-zentrierten Zielzustand angeglichen
- große Dateien laufen über schrittweises Line-Index- und Syntax-Warmup statt Vollscan im Draw
- vorhandene Lexer im Bestand: PlainText, C, C++, JavaScript, Python, JSON, Bash, zsh, Perl, MRMAC, Make, Markdown, Swift, Rust, Go, systemd et al.
- Markdown, Bash, zsh, Perl, Swift, Rust, Go und systemd et al. sind im Editorpfad zustandsbehaftet verdrahtet und nicht mehr nur baseline-artig angebunden

Erledigte Konsolidierung nach dem letzten Lexer-Block:

- der Dokumentkern wurde über den reinen Syntaxzug hinaus grundlegend stabilisiert:
  - der editierte Zustand verwendet wieder einen belastbaren exakten Zeilenindex
  - Whole-buffer-Hotpaths wurden aus kritischen Edit-Pfaden entfernt
  - Insert- und Erase-Pfade bleiben auch auf großen Dateien bedienbar
  - Block-Delete läuft nicht mehr über Vollsnapshot plus Whole-buffer-Replace
  - Undo-/Redo- und Restore-Pfade wurden aus den blockierenden Voll-Rebuilds herausgeführt
- die Quit- und Lebensdauerpfade wurden bereinigt:
  - `exit-discard` läuft nicht mehr in teure Nachläufe wie `longestLineWidth()`
  - Dirty-Gating ist wieder korrekt
  - der normale Quit-Pfad ist wieder praktisch sofort
- Syntax-Invalidation kappt überholte Warmup-Läufe und setzt die Prefetch-Frontier sauber zurück
- Smart Indent wurde für zsh, Perl und Markdown nachgeschärft
- heuristische Fold-Marker im Gutter sind ergänzt
- funktionales Folding-Backend ist im Editorpfad aktiv:
  - Fold-Spans
  - offene/geschlossene Folds
  - Fold-Kompression der Textansicht
  - dynamische mehrspaltige Gutter-Breite
  - Ebenenrahmen für geöffnete Strukturen
- die UI-seitigen Syntax-Reschedule-Oszillationen wurden beseitigt; `drawView()` und no-op-`scrollDraw()` stoßen keinen Syntax-Warmup mehr an
- Warmup-/Prefetch-Pending bleibt über sichtbare Task-Signale erkennbar
- MiniMap und Viewport werden bei sehr großen Dateien nach exakter Zeilenzahl enger nachgeführt
- aufgestaute Paging-Ereignisse bei gehaltenem `PageUp`/`PageDown` wurden durch Coalescing entschärft; der Editor bleibt dabei bedienbar
- automatische inhaltsheuristische Sprachklassifikation ist im Syntax-Modul umgesetzt
- `CODE_LANGUAGE=AUTO` ist im Runtime-Pfad wirksam verdrahtet
- der FE-Dialog führt die Sprachwahl als `Automatic`, intern bleibt die kanonische Serialisierung `AUTO`
- die erkannte Sprache wird im Markerblock des Frames als technisches Kürzel angezeigt
- `WINDOWCOLORS` wurde auf `v5` erweitert und enthält jetzt eine eigene `code folding marker`-Farbe mit Upgradepfad aus älteren Layouts
- das Folding-Gutter nutzt getrennte Markerfarbe statt nur der Folding-Hintergrundfarbe
- offene MR-DropLists übernehmen Mausrad-Ereignisse zentral statt sie an darunterliegende Listen weiterzureichen
- Folding-Sprachadapter wurden gruppenweise nachgeschärft:
  - `Python`, `zsh`, `Perl`
  - `Markdown`, `Make`
  - `JSON`, `JavaScript`
- `MRMAC` verwendet im Folding einen parser-treuen Strukturadapter für
  - `$MACRO ... END_MACRO`
  - `IF ... THEN ... END`
  - `IF ... THEN ... ELSE ... END`
  - `WHILE ... DO ... END`
- `MRMAC`-Makrogrenzen sind im Gutter sichtbar geklammert
- Fold-Cache und Fold-Scan sind sprachübergreifend von reiner Viewport-Teilmenge auf einen stabileren Scanbereich umgestellt, damit Aufwärts-/Abwärtsscrollen nicht verschiedene Strukturgrundlagen erzeugt
- der Fold-Bestand wird für bekannte Dokumentzeilenzahl sprachübergreifend dokumentweit aufgebaut und im Viewport nur noch projiziert; der Richtungsfehler beim Aufwärts-/Abwärtsscrollen ist damit im gemeinsamen Pfad beseitigt
- das gemeinsame Gutter-Rendering unterstützt jetzt explizite Schwesterzweige:
  - normaler Start `╭`
  - Schwesterzweig `├`
  - fortgesetzte Verzweigung `│`
  - Ende `╰`
- das Folding-Gutter nutzt jetzt ein einheitliches Klickmodell:
  - Linksklick toggelt den direkt getroffenen Fold
  - Rechtsklick toggelt alle sichtbaren Folds der getroffenen Spalte und aller Spalten rechts davon
- `trainers/foldtrainer/mrfoldtrainer` ist als separates Batchtool eingeführt und als regulärer Härtungspfad für Folding etabliert
- `trainers/indenttrainer/mrindenttrainer` ist als separates Batchtool eingeführt und als regulärer Härtungspfad für Smart Indent / Smart Undent etabliert
- batchtrainer-gestützt bis zu einem tragfähigen `v1.0`-Stand nachgezogen:
  - `MRMAC`
  - `Perl`
  - `JavaScript`
  - `C`
  - `C++`
  - `zsh`
  - `Swift`
  - `Rust`
  - `Go`
- Smart Indent wurde batchtrainer-gestützt zusätzlich nachgezogen für:
  - `bash`
  - `fish`
- `bash` und `zsh` sind als getrennte Sprachpfade mit getrennten Markern und getrennter Dateierkennung verdrahtet; `bash` ist nicht mehr nur ein `zsh`-Alias
- die `systemd`-Familie ist als eigene Sprachfamilie verdrahtet:
  - `.service`
  - `.socket`
  - `.timer`
  - `.mount`
  - `.automount`
  - `.target`
  - `.path`
  - `.slice`
  - `.scope`
  - `.swap`
  - `.device`
  - `.link`
  - `.netdev`
  - `.network`
- `systemd`-Sections falten familienweit am letzten echten Inhaltseintrag statt nachlaufende Kommentar- und Leerblöcke mitzuziehen
- der FE-Dialog ist auf den aktuellen Sprachbestand nachgeführt:
  - `zsh` ist sichtbar auswählbar
  - `systemd et al.` erscheint als sichtbarer Labeltext
- die File-Dialoge wurden im bestehenden TVision-Pfad nachgeschärft:
  - Pfadangabe im Dateinamenfeld navigiert per `Enter` in das Verzeichnis statt nur die History-Dropliste zu öffnen
  - `~` wird im Load-Dialog lokal expandiert
  - Doppelklick in der History-Dropliste lädt sofort
- der MFS-Ergebnisdialog wurde im Suchmodus nachgeschärft:
  - `Load` und `Load-All`
  - Doppelklick in der linken Trefferliste lädt sofort
  - der Fokus-Restore nach dem Schließen von Ergebnisdialog und MFS ist zentral über `mrActivateEditWindow(...)` gehärtet
- der aktuelle `v1.0`-Sprachstand ist im Maintainer-Lauf für folgende Sprachen als tragfähig bestätigt:
  - `bash`
  - `C`
  - `C++`
  - `JavaScript`
  - `Python`
  - `JSON`
  - `systemd et al.`
  - `zsh`
  - `Perl`
  - `MRMAC`
  - `Markdown`
  - `Swift`
  - `Rust`
  - `Go`
- `MRMAC` hat jetzt neben dem parser-treuen Folding auch einen eigenen Smart-Indent-/Smart-Undent-Pfad für
  - `$MACRO ... END_MACRO`
  - `IF ... THEN`
  - `ELSE`
  - `WHILE ... DO`
  - `END`
  - geschachtelte Blockkombinationen daraus
- Syntax, Folding und MiniMap laufen jetzt wieder gemeinsam auf einer bereinigten Foundation:
  - Syntax ist viewportnah reaktiviert und auch auf sehr großen Dateien bedienbar
  - Folding nutzt einen viewportzentrierten Up/Down-Scanner, arbeitet dateigrößenunabhängig und fällt nach Viewport-Ruhe sofort ab
  - die MiniMap ist wieder sichtbar, nutzt denselben gemeinsamen Scannerkern wie Folding und ist von der früheren globalen `pieceTableOnly`-Blockade entkoppelt
- die MiniMap-Reaktivität ist auf dieser Foundation nachgeschärft:
  - letzte brauchbare Projektion bleibt sichtbar
  - viewportnahe Requests werden sauberer nachgeführt
  - Overlay-Neuberechnung läuft nur noch bei echter Nutzänderung
- der Nicht-Brace-Bestand ist trainergestützt auf größere Realkorpora nachgezogen:
  - `bash`
  - `zsh`
  - `Perl`
  - `fish`
- breite Korpusrunden auf dem aktuell unterstützten Smart-Indent-Bestand haben zuletzt keine offenen belegten `strict`-Restfehler mehr ergeben

Aktueller Qualitätsstand:

- Inserts: sauber und auch in großer Menge bedienbar
- normale Deletes: sauber
- Block-Delete: sofort
- `PageUp` / `PageDown`: bedienbar, kein langer Event-Stau nach Loslassen
- Syntax: bedienbar auf sehr großen Dateien
- Folding: schnell genug und mit sauberem CPU-Abklingen
- MiniMap: funktional korrekt und sichtbar
- MiniMap-Reaktivität: auf der bestehenden Foundation ausreichend nachgezogen
- `exit-discard`: praktisch sofort

Noch nicht erledigt:

- bezogen auf diesen Plan besteht derzeit kein offener Pflichtpunkt mehr im bestehenden `v1.0`-Bestand
- automatische Sprachklassifikation zeigt derzeit keinen Handlungsbedarf; weitere Härtung bleibt Reserve für echte Fehlbefunde
- weitere Korpusrunden, neue Sprachaufnahmen oder zusätzliche UI-/Dialogpflege sind eigenständige Folgethemen und nicht mehr Teil dieses Restzugs
- ein Themenwechsel ist daher vertretbar, solange kein neuer belegter Fehlbefund aus dem aktuellen Bestand auftaucht

---

## Grundmodell

Pro Zeile werden gespeichert:

```text
LineStateIn
LineStateOut
TokenRuns
TextVersion
DirtyFlag
```

Nach einer Änderung wird ab der betroffenen Zeile neu gerechnet, bis sich der Ausgangsstate wieder stabilisiert.

Prinzip:

```text
Änderung in Zeile N
    -> Zeile N dirty
    -> ab vorherigem sicheren State neu lexen
    -> vorwärts laufen
    -> stoppen, sobald StateOut und TokenRuns wieder stabil sind
```

Das reicht allein nicht.

Der Lexer lebt nicht neben dem Textmodell, sondern auf ihm.

Zusätzlich braucht MR dokumentweite Invalidation:

```text
ChangedOffsetStart
ChangedOffsetEnd
FirstDirtyLine
LastKnownGoodCheckpoint
InvalidationReason
```

Ohne diese Dokument-Sicht endet das System wieder bei globalem "dirty"
oder unnötigem Relex über zu große Bereiche.

---

## Textmodell-Integration

Der eigentliche Engpass in MR war nicht nur Syntax, sondern wiederholt:

```text
Offset -> lineIndex
lineIndex -> lineStart
Viewport-Sprünge
EOF-Navigation
Minimap-Projektion
```

Der neue Syntaxpfad muss deshalb explizit an das Textmodell gekoppelt werden.

Benötigt werden:

```text
1. Lexer-Checkpoints sind an Dokument-Offsets und Zeilen gebunden.
2. Checkpoints dürfen nie stillschweigend exakte Global-Scans erzwingen.
3. Ein Viewport-Sprung darf auf vorhandenen Checkpoints aufsetzen.
4. Syntax-Checkpoints und Line-Index-Checkpoints müssen zusammenpassen,
   aber getrennte Rollen behalten.
```

Faustregel:

```text
Syntax darf das Textmodell benutzen.
Syntax darf das Textmodell nicht in teure Vollscans zwingen.
```

---

## Phase 1: Tree-sitter aus dem Rendering-Hotpath entfernen

Tree-sitter wird nicht sofort blind gelöscht, sondern zuerst entkoppelt.

Ziel:

```text
MRFileEditor darf nicht mehr direkt von Tree-sitter-Ergebnissen abhängen.
```

Neue Schicht:

```text
MRSyntaxHighlighter
MRSyntaxCache
MRSyntaxTokenRun
MRSyntaxLineState
```

Der Editor fragt künftig nur noch:

```text
Gib mir Token-Runs für Zeile N.
```

Nicht mehr:

```text
Gib mir AST-Nodes.
Gib mir Tree-sitter-Scopes.
Leite Syntax aus Root-Node ab.
Traversiere Syntaxbaum für Zielzeile.
```

---

## Phase 2: Datenmodell einführen

### Token-Klassen

```cpp
enum class MRSyntaxTokenKind {
    Normal,
    Keyword,
    TypeName,
    Number,
    StringLiteral,
    CharacterLiteral,
    Comment,
    Operator,
    Preprocessor,
    Macro,
    Builtin,
    Error
};
```

### Token-Run

Keine Einzelzeichen-Token, sondern kompakte Läufe:

```cpp
struct MRSyntaxTokenRun {
    uint32_t column;
    uint32_t length;
    MRSyntaxTokenKind kind;
};
```

Beispiel:

```cpp
if (value == 42) return true;
```

wird ungefähr:

```text
0..2      Keyword
3..8      Normal
9..11     Operator
12..14    Number
16..22    Keyword
23..27    Builtin/Keyword
```

---

## Phase 3: Line-State definieren

Der State muss klein, billig kopierbar und billig vergleichbar sein.

```cpp
enum class MRSyntaxMode : uint16_t {
    Normal,
    BlockComment,
    StringLiteral,
    RawStringLiteral,
    CharacterLiteral,
    Preprocessor,
    HereDocument
};

struct MRSyntaxLineState {
    MRSyntaxMode mode;
    uint16_t flags;
    uint32_t payload;
};
```

`payload` kann später sprachspezifische Details tragen:

```text
Raw-String-Delimiter-ID
Here-Doc-ID
verschachtelter Kommentarlevel
```

Wichtig:

```cpp
bool operator==(const MRSyntaxLineState &, const MRSyntaxLineState &);
```

Ohne billigen State-Vergleich gibt es keine saubere Konvergenz.

---

## Phase 4: Syntax-Cache pro Zeile

```cpp
struct MRSyntaxLineCacheEntry {
    MRSyntaxLineState stateIn;
    MRSyntaxLineState stateOut;
    std::vector<MRSyntaxTokenRun> tokenRuns;
    uint64_t textVersion;
    bool dirty;
};
```

Später kann `std::vector` durch eine kompaktere Struktur ersetzt werden. Nicht zuerst.

Priorität zuerst:

```text
korrektes Modell
saubere Invalidation
stabile Konvergenz
Renderer-Anbindung
```

---

## Phase 5: Highlighter-Interface

Das Interface bleibt MR-eigen.

```cpp
class MRSyntaxHighlighter {
public:
    virtual ~MRSyntaxHighlighter() = default;

    virtual MRSyntaxLineResult highlightLine(
        std::string_view line,
        MRSyntaxLineState previousState
    ) = 0;
};
```

Ergebnis:

```cpp
struct MRSyntaxLineResult {
    MRSyntaxLineState stateOut;
    std::vector<MRSyntaxTokenRun> tokenRuns;
};
```

Der Editor darf nicht wissen, ob darunter ein handgeschriebener Lexer, lex/flex, re2c oder später Lexilla arbeitet.

---

## Phase 6: Invalidation und Konvergenz

Nach jeder Textänderung:

```text
1. erste betroffene Zeile bestimmen
2. diese Zeile dirty setzen
3. Startpunkt bestimmen:
       betroffene Zeile
       oder vorheriger gültiger Checkpoint
4. Zeile für Zeile neu lexen
5. pro Zeile vergleichen:
       alter StateIn
       alter StateOut
       alte TokenRuns
       neue Werte
6. stoppen, sobald sich Ausgangsstate und TokenRuns wieder stabilisieren
```

Nicht jeder Unterschied ist gleich wichtig.

Der Plan braucht drei Vergleichsebenen:

```text
1. state-equivalent
2. render-equivalent
3. checkpoint-equivalent
```

Das verhindert unnötigen Churn.

Beispiel:

```text
TokenRuns intern anders
aber gleiche sichtbare Farben und gleiche StateOut-Semantik
-> kein weiterer Relex-Lauf nötig
```

Beispiel:

```cpp
/* Kommentar beginnt hier
   und endet später */
```

Wenn in der ersten Zeile `/*` gelöscht wird, können viele Folgezeilen betroffen sein.

Bei normalem Tippen in einer normalen Codezeile endet der Relex meist nach einer oder wenigen Zeilen.

---

## Cache-Commit-Regeln

Background- oder spekulative Ergebnisse dürfen nicht blind übernommen werden.

Jedes Ergebnis muss beim Commit prüfen:

```text
DocumentVersion
CheckpointGeneration
StateIn
betroffener Zeilenbereich
```

Nur wenn diese Eingaben noch gültig sind, darf das Ergebnis in den globalen
Cache geschrieben werden.

Veraltete Ergebnisse werden:

```text
nicht gemerged
nicht halb angewendet
nicht UI-sichtbar gemacht
sondern verworfen
```

Wichtig:

```text
Ein Worker erzeugt nur Kandidaten.
Der Cache-Commit ist autoritativ und versionsgebunden.
```

---

## Phase 7: Sichtbaren Bereich priorisieren

Der sichtbare Bereich hat Vorrang.

Beim Draw:

```text
1. sichtbare Zeilen bestimmen
2. fehlende oder dirty Token-Runs mit kleinem Budget berechnen
3. falls Budget überschritten:
       alte Token-Runs verwenden
       oder PlainText rendern
4. Hintergrund-Relex anstoßen
```

Nicht erlaubt:

```text
Draw wartet auf vollständigen Datei-Relex.
Draw startet kompletten Scanner über die Datei.
Draw blockiert auf Worker-Ergebnis.
Draw traversiert Parserbäume.
```

Ziel:

```text
Viewport first.
Correct enough immediately.
Perfect later.
```

---

## Phase 8: Erste Highlighter

Zuerst:

```text
MRPlainTextHighlighter
MRMRmacSyntaxHighlighter
MRCppSyntaxHighlighter
```

### PlainTextHighlighter

Dient zum Testen von:

```text
Interface
Cache
Renderer
Invalidation
Dirty-Intervalle
```

### MRMAC-Highlighter

Der erste echte stateful Highlighter sollte MRMAC sein.

Begründung:

```text
kleinere Sprachfläche
direkter Projektnutzen
schnelleres Debugging von State, Checkpoints und Invalidation
geringerer Fehlerradius als C++
```

### C++-Highlighter

Zunächst robust, aber nicht vollständig semantisch.

Unterstützen:

```text
Kommentare
Strings
Chars
Raw Strings
Zahlen
Keywords
Präprozessor
Operatoren
Identifier
```

Nicht erforderlich am Anfang:

```text
Template-Verständnis
Typauflösung
Namespace-Verständnis
Funktionsanalyse
AST
Semantik
```

---

## Phase 9: lex/flex als Backend prüfen

Der Primärpfad ist ein MR-eigener handgeschriebener Lexer.

lex/flex ist höchstens ein optionales Sprach-Backend, nicht gleichrangige
Kernarchitektur.

Richtig nur dann:

```text
MRFlexCppHighlighter
    nimmt eine Zeile + MRSyntaxLineState
    setzt flex-Startbedingung
    scannt genau diese Zeile
    liefert Token-Runs + neuen MRSyntaxLineState
```

Falsch:

```text
ganze Datei an yylex verfüttern
Scanner im Draw erzeugen
flex-Zustände direkt im Editor speichern
Editor speichert YY_BUFFER_STATE
Editor kennt BEGIN(...)
```

MR-State bleibt autoritativ:

```text
MRSyntaxLineState <-> flex start condition
```

Bevorzugt:

```text
MR-eigener Lexer
voll kontrollierte Zustände
voll kontrollierte Invalidation
keine Generator-Abhängigkeit im Kernmodell
```

Bison bleibt aus dem Live-Highlighting heraus.

Möglicher Einsatz von Bison nur für:

```text
MR-Makrosprache
settings.mrmac
Kommandosyntax
statische Analyse
Formatprüfung
```

---

## Phase 10: Background-Relex

Nach stabilem Viewport-Modell wird Hintergrundarbeit ergänzt.

Zunächst seriell mit Zeitbudget:

```text
pro UI-Tick maximal X Millisekunden Syntaxarbeit
```

Aufgaben:

```text
dirty Intervalle verkleinern
Checkpoints aktualisieren
entfernte Bereiche vorbereiten
große Dateien stückweise aufwärmen
```

Regel:

```text
Der Hintergrund darf nie Voraussetzung für korrektes Tippen oder Scrollen sein.
```

---

## Phase 11: Checkpoints

Für große Dateien werden Anker eingeführt.

Beispiel:

```text
Zeile 0       Checkpoint
Zeile 512     Checkpoint
Zeile 1024    Checkpoint
Zeile 1536    Checkpoint
```

Beim Sprung zu Zeile 1200:

```text
Checkpoint 1024 suchen
ab dort bis 1200 lexen
Viewport rendern
```

Bei Änderungen vor einem Checkpoint werden spätere Checkpoints nicht hektisch gelöscht, sondern als potentiell veraltet markiert:

```text
Checkpoint may be stale
```

Beim Bedarf wird validiert oder ersetzt.

---

## Phase 12: Parallelisierung

Parallelisierung wird von Anfang an architektonisch vorbereitet, aber nicht zuerst implementiert.

Nicht machen:

```text
eine Task pro Zeile
Worker im Draw-Pfad
aggressive Work-Stealing-Logik
UI wartet auf Worker
```

Machen:

```text
große unabhängige Blöcke
Background-Aufarbeitung entfernter Bereiche
Übernahme nur bei gültigem Eingangsstate
```

Beispiel:

```text
Block 0: 0..511
Block 1: 512..1023
Block 2: 1024..1535
```

Ein Block darf nur starten, wenn sein Eingangsstate bekannt ist.

Spekulativ möglich:

```text
Block mit altem Checkpoint-State rechnen
Ergebnis nur übernehmen, wenn der Checkpoint noch gültig ist
sonst verwerfen und neu einplanen
```

Zusätzlich muss der Commit-Punkt explizit geregelt sein:

```text
Worker rechnet Kandidat
Hauptcache prüft Version + Checkpoint + StateIn
nur dann Commit
sonst Drop
```

Wichtig:

```text
kein Baum
kein Merge
keine Node-Invalidierung
kein Root-Traversal
```

Nur:

```text
Token-Runs
LineStateIn
LineStateOut
```

---

## Phase 13: Qualitätsstufen

Die Pipeline hat bewusst mehrere Qualitätsstufen.

### Stufe 0: Plain

```text
keine Syntaxdaten verfügbar
-> normal rendern
```

### Stufe 1: Viewport lexical

```text
sichtbare Zeilen lexikalisch korrekt genug
```

### Stufe 2: Cached lexical

```text
umliegende Bereiche vorbereitet
schnelles Scrollen
```

### Stufe 3: Full warm cache

```text
Datei weitgehend durchlexed
Checkpoints stabil
```

### Stufe 4: optionale Struktur

Später, getrennt vom Farb-Hotpath:

```text
Symbolübersicht
Funktionsliste
Klammernavigation
Fold-Bereiche
```

Diese Strukturstufe ist ein eigenes Backend.

Sie gehört nicht in den Primärpfad des Lexers und nicht in den Sofort-Draw.

---

## Phase 14: Sprachprioritäten

Empfohlene Reihenfolge:

```text
1. Plain Text
2. MR-Makros
3. C++
4. Perl
5. Shell
6. JSON
7. Markdown
8. CMake/Makefile
```

Für jede Sprache gilt dasselbe Interface.

Keine Sonderwege im Editor.

---

## Phase 15: Tests und Messpunkte

Regressionstests prüfen keine subjektiv schönen Farben, sondern harte Eigenschaften.

### Korrektheit

```text
mehrzeiliger Kommentar stabilisiert korrekt
String über Zeile färbt Folgezeile korrekt
Raw String endet korrekt
Änderung in Zeile N beeinflusst nur nötigen Bereich
State-Konvergenz stoppt
```

### Performance

Messen:

```text
Zeit für Viewport-Relex
Zeit für Änderung in normaler Zeile
Zeit für Änderung am Beginn eines großen Blockkommentars
Speicher pro Zeile
Token-Runs pro 1000 Zeilen
```

### Freeze-Schutz

Harte Grenzen:

```text
maximale Arbeit pro Draw
maximale Token-Runs pro Zeile
maximale zu scannende Zeichen pro UI-Zyklus
Fallback auf PlainText
```

Das ist keine Wächterarchitektur, sondern reguläre Budgetierung der Renderpipeline.

---

## Phase 16: Editor-Konsolidierung nach dem Lexer-Block

Diese Tranche wird bewusst vor einer automatischen Sprachklassifikation einsortiert.

Begründung:

```text
erst Bedien- und Large-File-Pfad konsolidieren
danach Sprachklassifikation auf stabiler Infrastruktur aufsetzen
```

Erledigt:

```text
1. heuristische Fold-Marker im Gutter
2. sichtbare Warmup-/Prefetch-Task-Signale
3. MiniMap-/Viewport-Abstimmung bei sehr großen Dateien
4. Smart-Indent-Nachschärfung für weitere Sprachsonderfälle
```

Wichtig:

```text
Diese Phase hat nur die heuristische Gutter-Vorstufe geliefert.
Sie ist ausdrücklich noch kein funktionales Folding-Backend.
```

---

## Phase 17: Inhaltsheuristische Sprachklassifikation

Diese Phase wird nicht vorgezogen. Sie folgt auf die abgeschlossene Editor-Konsolidierung.

Ziel:

```text
Dateityp nicht nur über Endung,
sondern über ein kleines hochsignifikantes Konstrukt-Subset pro Sprache bestimmen
```

Regeln:

- nur trennscharfe Konstrukte mit hoher Diskriminationskraft werten
- Treffer in Kommentaren und Strings nach Möglichkeit nicht mitzählen
- Dateiname, Endung und Shebang bleiben starke Zusatzsignale
- Konfliktsignale und Negativsignale aktiv berücksichtigen
- bei knapper Lage konservativ auf PlainText oder bestehende Endungsheuristik zurückfallen
- Code-Reuse bleibt Pflicht: gemeinsamer Extraktor für Shebang, Basename, Delimiter-Signaturen, Keyword-Sets, Here-Doc-/Fence-/Triple-Quote-Signale, Score-Akkumulation und Confidence-Regeln
- für die Mengenarbeit der Erkennungsmerkmale sind C++-Set-Operatoren in dieser Phase ausdrücklich freigegeben

Gemeinsames Klassifikationsmodell:

```text
1. Basis-Signale einsammeln
   - Dateiendung
   - Basename
   - Titelhinweis
   - Shebang
2. nur ein kleines Sprach-Subset pro Lexer prüfen
3. gewichtete Positiv- und Negativtreffer aufsummieren
4. Konfidenz pro Sprache bilden
5. nur bei hoher Konfidenz automatisch setzen
```

Erledigt:

```text
1. gemeinsames, scorebasiertes Klassifikationsmodell im Syntax-Modul
2. Sprachsubsets pro gelexter Sprache mit Dateiname-/Endung-/Shebang-Zusatzsignalen
3. konservativer Fallback auf PlainText bzw. Endungsheuristik bei schwacher Lage
4. Konfliktsignale, Negativsignale und konservativere Konfidenzschwellen sind heuristisch nachgeschärft
5. Runtime-Verdrahtung von CODE_LANGUAGE=AUTO
6. FE-Dialog zeigt Automatic, intern bleibt AUTO kanonisch
7. erkannte Sprache wird im Frame-Markerblock sichtbar
```

Offen:

```text
1. systematische Fehlklassifikationssammlung gegen echte Problemdateien
2. weitere Nachschärfung der Gewichte und Konfliktsignale auf Basis echter Fehlbefunde
3. optionale Ausweitung von Marker/Confidence auf spätere UI-Entscheidungen nur bei echtem Bedarf
```

---

## Phase 18: Strukturelles Folding-Backend

Diese Phase folgt bewusst auf die Sprachklassifikation und steht inhaltlich auf der bereits früher benannten
`Stufe 4: optionale Struktur`.

Begründung:

```text
erst line-based Lexer-, Cache- und Klassifikationsbasis stabilisieren
dann Strukturinformationen als eigenes Backend ergänzen
```

Ziel:

```text
funktionales Folding über alle unterstützten Sprachen
ohne Parser im Draw-Hotpath
ohne Vermischung mit dem Farbpfad
```

Kernmodell:

```text
FoldSpan
    StartLine
    EndLine
    Level
    OpenOrClosed
    SourceKind
```

`SourceKind` ist dabei keine neue UI-Semantik, sondern nur die Herkunft der Struktur:

```text
DelimiterBlock
IndentBlock
FenceBlock
HereDocumentBlock
CommentBlock
DirectiveBlock
LanguageSpecific
```

Architekturregel:

```text
der Lexer liefert Struktursignale
das Folding-System baut daraus Fold-Spans
der Editor rendert nur den vorbereiteten Fold-Zustand
```

Nicht erlaubt:

```text
Klammer- oder Strukturparsing im Draw
globaler Vollscan pro Repaint
Scheinsemantik mit collapse/expand-Glyphen ohne echten Fold-Zustand
```

Gutter-Anforderungen:

```text
1. der Folding-Gutter wird mehrspaltig
2. seine Breite wächst und schrumpft automatisch mit der maximal sichtbaren Schachtelung
3. eingeklappt zeigt ein Fold-Start ein Dreieck nach rechts
4. aufgeklappt zeigt der Gutter keine Dreiecke, sondern Ebenenrahmen
5. pro Ebene wird eine eigene Gutter-Spalte verwendet
```

Rahmen-Darstellung für aufgeklappte Ebenen:

```text
Beginn: linke obere Single-Line-Ecke
Mitte: vertikaler Single-Line-Strich
Ende: linke untere Single-Line-Ecke
```

Diese Darstellung wird pro geschachtelter Ebene in einer eigenen Gutter-Spalte geführt.

Sprachübergreifende Abdeckung:

```text
C / C++ / JavaScript / JSON:
    Delimiter- und Directive-Blöcke

Python / zsh / Make:
    Indent-Blöcke

Markdown:
    Fence- und Abschnittsblöcke

Perl:
    Delimiter-, HereDoc- und POD-nahe Strukturbereiche

MRMAC:
    Block- und Delimiterstrukturen
```

Stand:

```text
erledigt:
1. Fold-Datenmodell und Invalidationsregeln
2. Span-Berechnung aus sprachspezifischen Struktursignalen
3. dynamische mehrspaltige Gutter-Geometrie
4. Renderpfad für eingeklappte Marker und aufgeklappte Ebenenrahmen
5. Bedienung für Auf- und Zuklappen
6. sprachübergreifend stabiler dokumentweiter Fold-Cache statt viewportabhängiger Richtungsartefakte
7. Branch-Rendering mit expliziten Schwesterzweigen (`├`) im gemeinsamen Gutter-Pfad
8. Batchtrainer-gestützte Sprachhärtung bis zu einem tragfähigen `v1.0`-Stand für die priorisierten Editor-Sprachen

offen:
9. restliche Sprachbestände und neue Sprachen im selben Batchtrainer-Verfahren nachziehen
10. systematische Vollabnahme über den Sprachbestand und größere Problemdatei-Mengen
```

Wichtig:

```text
funktionales Folding ist ein eigenes Strukturbackend
und keine Nebenwirkung des Syntax-Colorings
```

---

## Kombinierter Zug 19 + 20: Inkrementelle Gültigkeitsbereiche statt Hotpath-Warmups

Die bisherige Spätphase wird ersetzt.

Grund:

```text
die bisherige Coprocessor-/Warmup-Orchestrierung ist zu hypertroph geworden
und hat den Editorpfad nicht zuverlässig entlastet
```

Neue Grundregel:

```text
alle Dateien werden gleich behandelt
es gibt keine Sonderpfade nach Dateigröße
```

Die Unterscheidung zwischen kleinen und großen Dateien hat

```text
zu viele Scheduler-, Viewport- und Invalidation-Sonderfälle erzeugt
```

und ist damit selbst Teil des Performanceproblems geworden.

Neues Zielbild:

```text
Dokument bleibt Primärzustand
Syntax, Folding und MiniMap sind drei abgeleitete Zustandsstores
jeder Zustandsstore führt dokumentweite Gültigkeits- und Ungültigkeitsbereiche
nach Edit wird nur invalidiert
die UI darf stale Daten weiter anzeigen oder vorübergehend lückenhaft sein
aber sie darf nicht auf Hintergrundauffüllung warten
```

Kernaussage:

```text
Tippen hat Vorrang vor Decorator-Systemen
```

Das bedeutet ausdrücklich:

```text
1. Syntax darf pro Zeichen nicht erneut den sichtbaren Bereich vollständig anwärmen
2. Folding darf seine Struktur nicht aus viewportnahen Hotpath-Scans wiederholt neu ableiten
3. MiniMap darf nach Edit nicht sofort wieder in den Interaktionspfad zurückdrücken
```

### Architekturprinzip

Die PieceTable-Idee wird nicht kopiert, aber konzeptionell wiederholt:

```text
stabiler Primärzustand
plus inkrementell gepflegte Zusatzdaten
plus explizite Gültigkeitsbereiche
```

Für die drei abgeleiteten Systeme bedeutet das:

```text
Syntax:
    Token-/Run-Cache pro Zeile
    State-Checkpoints für stateful languages
    dokumentweite Gültigkeitsintervalle

Folding:
    dokumentweite Fold-Span-Truth
    getrennte Viewport-Projektion
    dokumentweite Gültigkeitsintervalle

MiniMap:
    globaler Darstellungszustand
    dokumentweite Sampling-/Line-Validität
    getrennte sichtbare Projektion
```

Wichtig:

```text
das ist keine dreifache PieceTable
sondern dieselbe Betriebslogik für abgeleitete Daten
```

### Rückbau des Coprocessors

Der Coprocessor bleibt erlaubt, aber nur in einer stark vereinfachten Rolle:

```text
schlichter Hintergrund-Ausführer
nicht Orchestrator des Editor-Hotpaths
```

Das heißt:

```text
kein hektisches pro-Edit-Taskleben
keine dateigrößenabhängigen Sonderscheduler
keine versionsgetriebene Warmup-Sturmserie im Editorpfad
kein erneutes Anwerfen von Syntax/Folding/MiniMap nur weil ein Commit passiert ist
```

Hintergrundarbeit bleibt notwendig,

```text
aber sie wird nur aus Invaliditätsbereichen gespeist
nicht aus jeder sichtbaren Sofortinteraktion
```

### Zug 19 neu

Zug 19 stellt Syntax und Folding auf die gemeinsame Inkrementalmechanik um.

Umfang:

```text
Syntax:
    Cache, Checkpoints, Validitätsintervalle
    keine Hotpath-Warmups pro Edit
    Viewport fordert nur fehlende Teilbereiche an

Folding:
    dokumentweite Fold-Spans als Wahrheitszustand
    Viewport nur noch als Projektion
    keine erneute globale oder viewportgetriebene Strukturermittlung im Scroll-/Edit-Hotpath

Coprocessor:
    bestehende Warmup-Orchestrierung deutlich zurückbauen
    nur noch gezielte Hintergrundauffüllung invalidierter Bereiche
```

### Zug 20 neu

Zug 20 stellt MiniMap auf dieselbe Mechanik um.

Umfang:

```text
MiniMap:
    globaler Renderzustand mit Validitätsbereichen
    voller Erstscan im Hintergrund ist zulässig
    nach Edit nur Invalidierung betroffener Bereiche
    keine Sofort-Neuberechnung im Interaktionspfad

UI:
    letzte gültige MiniMap-Projektion darf stehen bleiben
    unvollständige Anzeige ist zulässig
    blockierendes Nachziehen ist unzulässig
```

### Praktische Konsequenz für den Editorpfad

Nach einem Commit gilt:

```text
1. ChangeSet auswerten
2. in Syntax/Folding/MiniMap nur Gültigkeitsbereiche beschneiden
3. Cursor/Viewport normal fortsetzen
4. sichtbare Darstellung mit bereits vorhandenem Zustand zeichnen
5. Hintergrund nur für fehlende Bereiche anfordern
```

Nicht mehr erlaubt:

```text
Commit -> sofort Syntax neu planen
Commit -> sofort Folding erneut anstoßen
Commit -> sofort MiniMap wieder aufwecken
Commit -> sichtbaren Bereich pro Zeichen vollständig nachwärmen
```

### Prüfkriterien

Die kombinierte Phase ist erst erfolgreich, wenn:

```text
1. Tippen in derselben Datei nicht progressiv langsamer wird
2. Syntax pro Zeichen nicht denselben Bereich erneut vollständig aufweckt
3. Folding unabhängig von Dateigröße kein Scroll- oder Eingabelag erzeugt
4. MiniMap an/aus den Editor nicht mehr zwischen bedienbar und unbedienbar kippen lässt
5. stale oder verspätete Darstellung akzeptabel ist, Eingabelag aber nicht
```

Nicht erlaubt:

```text
neue Größen-Sonderpfade
neue Warmup-/Pending-/Viewport-Instabilität
CPU-Auslastung als Selbstzweck
Verlust von Korrektheit zugunsten scheinbarer Responsivität
```

Erfolgskriterium:

```text
ein einheitlicher, deutlich schlankerer Editorpfad
ohne Dateigrößenverzweigung
mit strikt vom Tippen entkoppelter Hintergrundauffüllung
```

### Erkennungs-Subset je Sprache

`PlainText`

- kein Positiv-Subset
- dient als konservativer Fallback, wenn keine Sprache die Konfidenzschwelle erreicht

`C`

- `#include`, `#define`, `#ifdef`, `#ifndef`
- `struct`, `enum`, `typedef`
- `->`
- Blockkommentare `/* ... */`

`C++`

- `namespace`, `class`, `template<`
- `::`
- `typename`, `constexpr`
- Raw Strings `R"delim(... )delim"`

`JavaScript`

- `import ... from`
- `export`
- `const`, `let`
- `=>`
- Backtick-Strings

`Python`

- Shebang mit `python`
- `def`, `class`
- `elif`, `except`, `async def`
- Triple-Quoted Strings
- Blockkopf mit `:`

`JSON`

- String-Key gefolgt von `:`
- dichte Folge aus `{}`, `[]`, `,`, `:`
- Literale `true`, `false`, `null`
- Negativsignal: starke Treffer für Shell-, Perl- oder C-artige Kontrollsyntax

`zsh`

- Shebang mit `zsh`
- `${...}`
- `$()`
- `[[ ... ]]`
- `case ... in`
- `typeset`, `autoload`, `setopt`
- Here-Docs `<<EOF`, `<<-EOF`

`bash`

- Shebang mit `bash`, `sh`, `ksh`
- `${...}`
- `$()`
- `[[ ... ]]`
- `case ... in`
- `declare`, `readonly`, `shopt`, `source`
- Here-Docs `<<EOF`, `<<-EOF`

`systemd`

- Dateifamilie `.service`, `.socket`, `.timer`, `.mount`, `.automount`, `.target`, `.path`, `.slice`, `.scope`, `.swap`, `.device`, optional `.link`, `.netdev`, `.network`
- Section-Header wie `[Unit]`, `[Service]`, `[Install]`, `[Network]`
- Schlüssel links von `=`
- Kommentare mit `#` und `;`
- Folding pro Section-Block
- flaches Indent ohne aggressive Blockheuristik

`Perl`

- Primärtrigger `.pl`, `.pm`
- Sigils `$`, `@`, `%`
- `my`, `our`, `sub`, `package`, `use`
- POD `=pod ... =cut`
- `s///`, `tr///`, `y///`, `qr//`
- Here-Docs

`MRMAC`

- Direktiven mit `$...`
- verschachtelte Kommentarblöcke `{ ... }`
- Winkelklammer-Konstrukte `<...>`
- charakteristische Schlüsselwörter und Typwörter des MRMAC-Lexers
- Single-Quote-Strings mit verdoppeltem Escape `''`

`Make`

- Basenames `Makefile`, `GNUmakefile`
- Target-Zeile `name: deps`
- Rezeptzeilen mit führendem Tab
- Variablenformen `$(...)`, `${...}`
- Zuweisungen `=`, `:=`, `?=`, `+=`
- `.PHONY`

`Markdown`

- ATX-Headings `#`
- Setext-Headings mit `===` oder `---`
- Fenced Code Blocks mit drei Backticks oder `~~~`
- Blockquotes `>`
- Listenmarker `-`, `*`, `+`, `1.`
- Links, Referenzlinks, Bilder, Autolinks
- Tabellen-Trennzeilen

Diese Subsets sind absichtlich klein gehalten.

Nicht Ziel:

```text
die ganze Sprache zu erkennen
```

Ziel:

```text
mit wenigen hochsignifikanten Treffern eine belastbare automatische Sprachwahl zu erreichen
```

---

## Grobe Umsetzungsreihenfolge

```text
1. Syntax-Interfaces und Token-Datenmodell einführen
2. PlainTextHighlighter anschließen
3. Renderer auf Token-Runs umstellen und Tree-sitter gleichzeitig aus dem Draw-Pfad entfernen
4. SyntaxCache pro Zeile einführen
5. Dirty-Intervalle, Dokument-Invalidation und Relex-Konvergenz bauen
6. Checkpoint- und Commit-Regeln definieren
7. MR-Makro-Highlighter bauen
8. einfachen C++-Highlighter bauen
9. Background-Relex mit Zeitbudget ergänzen
10. Checkpoints für große Dateien ergänzen
11. lex/flex-Backend nur bei echtem Sprachbedarf prüfen
12. Parallel-Relex für große Bereiche ergänzen
13. Tree-sitter-Code endgültig entfernen oder optional isoliert deaktiviert lassen
14. Editor-Konsolidierung nach dem Lexer-Block:
    Fold-Gutter, Task-Signale, MiniMap-/Viewport-Abstimmung, Smart-Indent-Nachschärfung
15. automatische inhaltsheuristische Sprachklassifikation auf Basis der definierten Sprach-Subsets
16. Härtung der Sprachklassifikation gegen Problemdateien und Fehlklassifikationen
17. strukturelles Folding-Backend mit Fold-Spans, dynamischer Gutter-Breite und Ebenenrahmen
18. Batchtrainer-gestützte Härtung des Folding-Backends gegen Sprachsonderfälle, Richtungsfehler und reale Korpora bis zum tragfähigen `v1.0`-Sprachset
19. kombinierter Architekturzug:
    Syntax und Folding auf dokumentweite Gültigkeitsbereiche,
    Checkpoints und stale-while-revalidate umstellen;
    Coprocessor-Orchestrierung deutlich zurückbauen;
    keine Dateigrößen-Sonderpfade mehr
20. MiniMap auf dieselbe Inkrementalmechanik umstellen:
    globaler Darstellungszustand,
    dokumentweite Gültigkeitsbereiche,
    reine Hintergrundauffüllung ohne Eingabe-Hotpath-Kopplung
```

---

## Architekturregel

Syntax-Highlighting ist eine Cache-Schicht über Zeilen, kein Parser-Subsystem im Editor-Kern.

Das verhindert:

```text
Root-Traversierung pro Zielzeile
AST-Abhängigkeit im Renderer
globale Analyse vor sichtbarer Ausgabe
Freezes durch Parserzustände
Baum-Zusammenführen
```

Das Ziel ist ein Syntaxsystem, das schnell, robust, inkrementell und notfalls degradierbar ist.
