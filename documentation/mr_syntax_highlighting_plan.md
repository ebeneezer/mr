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
- vorhandene Lexer im Bestand: PlainText, C, C++, JavaScript, Python, JSON, zsh, Perl, MRMAC, Make, Markdown
- Markdown, zsh und Perl sind im Editorpfad zustandsbehaftet verdrahtet und nicht mehr nur baseline-artig angebunden

Erledigte Konsolidierung nach dem letzten Lexer-Block:

- Syntax-Invalidation kappt überholte Warmup-Läufe und setzt die Prefetch-Frontier sauber zurück
- Smart Indent wurde für zsh, Perl und Markdown nachgeschärft
- heuristische Fold-Marker im Gutter sind ergänzt
- Warmup-/Prefetch-Pending bleibt über sichtbare Task-Signale erkennbar
- MiniMap und Viewport werden bei sehr großen Dateien nach exakter Zeilenzahl enger nachgeführt
- automatische inhaltsheuristische Sprachklassifikation ist im Syntax-Modul umgesetzt
- `CODE_LANGUAGE=AUTO` ist im Runtime-Pfad wirksam verdrahtet
- der FE-Dialog führt die Sprachwahl als `Automatic`, intern bleibt die kanonische Serialisierung `AUTO`
- die erkannte Sprache wird im Markerblock des Frames als technisches Kürzel angezeigt

Noch nicht erledigt:

- Goldstandard für alle begonnenen Sprachen ist nur für einen Teil des Bestands erreicht
- automatische Sprachklassifikation ist noch nicht über systematische Problemdatei-Mengen und Fehlklassifikationsmetriken gehärtet

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

- Shebang mit `zsh`, `sh`, `bash`, `ksh`
- `${...}`
- `$()`
- `[[ ... ]]`
- `case ... in`
- `typeset`, `autoload`, `setopt`
- Here-Docs `<<EOF`, `<<-EOF`

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
