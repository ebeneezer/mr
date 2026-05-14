# Vorgehensweise: UTF-8-Text nach PDF exportieren mit Pango + Cairo

## Ziel

Ein C++-Exporter soll UTF-8-behafteten Text als PDF ausgeben.

Dabei sollen beachtet werden:

- ein konfigurierbares Seitentrenner-Literal
- ein rechter Print-Margin
- saubere UTF-8-Ausgabe
- mehrseitige PDF-Erzeugung
- möglichst wenig eigene Textlayout-Logik
- lizenzrechtlich unkritische Nutzung

## Empfehlung

Für diese Aufgabe ist **Pango + Cairo** die sinnvollste Kombination.

- **Cairo** erzeugt die PDF-Datei.
- **Pango** übernimmt UTF-8-Textlayout, Fontauswahl, Glyph-Shaping und Zeilenumbruch.
- **PangoCairo** verbindet beides.

Eine reine PDF-Bibliothek wäre hier zu niedrig angesetzt, weil sie meist nur Text an Koordinaten schreibt. Den eigentlichen Zeilenumbruch und das Layout müsste man dann selbst implementieren.

## Installation unter CachyOS / Arch

```zsh
sudo pacman -S cairo pango
```

Optional, wenn C++-Wrapper gewünscht sind:

```zsh
sudo pacman -S cairomm pangomm
```

Empfehlung für dieses konkrete Projekt: zunächst die C-APIs direkt aus C++ verwenden. Sie sind stabil, dokumentiert und vermeiden zusätzliche Wrapper-Abhängigkeiten.

## Grundarchitektur

Vorgeschlagene Klasse:

```cpp
class MRPdfTextExporter {
public:
    struct Settings {
        std::string outputPath;
        std::string pageSeparatorLiteral = "\f";

        double pageWidthPoints = 595.0;   // A4 Breite
        double pageHeightPoints = 842.0;  // A4 Höhe

        double leftMarginPoints = 50.0;
        double rightMarginPoints = 50.0;
        double topMarginPoints = 50.0;
        double bottomMarginPoints = 50.0;

        std::string fontDescription = "DejaVu Sans Mono 10";

        bool usePrintMarginColumns = true;
        int printMarginColumns = 120;
    };

    bool exportText(const std::string& utf8Text, const Settings& settings);
};
```

## Verarbeitungsschritte

### 1. Text in logische Seiten zerlegen

Der Eingabetext wird anhand des Seitentrenner-Literals geteilt.

Beispiel:

```text
erste Seite
---PAGE---
zweite Seite
---PAGE---
dritte Seite
```

Bei `pageSeparatorLiteral = "---PAGE---"` entstehen daraus drei logische Seiten.

Wichtig: Das Literal selbst wird nicht gedruckt.

### 2. PDF-Surface erzeugen

Cairo erzeugt ein PDF-Surface:

```cpp
cairo_surface_t* surface =
    cairo_pdf_surface_create(
        settings.outputPath.c_str(),
        settings.pageWidthPoints,
        settings.pageHeightPoints
    );

cairo_t* cr = cairo_create(surface);
```

### 3. Pango-Layout erzeugen

```cpp
PangoLayout* layout = pango_cairo_create_layout(cr);
PangoFontDescription* font =
    pango_font_description_from_string(settings.fontDescription.c_str());

pango_layout_set_font_description(layout, font);
```

### 4. Nutzbare Textbreite bestimmen

Die physisch verfügbare Breite:

```cpp
double usableWidth =
    settings.pageWidthPoints
    - settings.leftMarginPoints
    - settings.rightMarginPoints;
```

Wenn der rechte Print-Margin als Spaltenbegrenzung verstanden wird, sollte zusätzlich die Spaltenbreite berücksichtigt werden.

Bei Monospace-Fonts:

```cpp
double effectiveWidth = usableWidth;

if (settings.usePrintMarginColumns) {
    double approximateCharacterWidth = estimateMonospaceCharacterWidth(...);
    double columnWidth = settings.printMarginColumns * approximateCharacterWidth;
    effectiveWidth = std::min(usableWidth, columnWidth);
}
```

Danach wird die Breite an Pango übergeben:

```cpp
pango_layout_set_width(layout, effectiveWidth * PANGO_SCALE);
pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
```

Für Code/Textausgabe ist `PANGO_WRAP_WORD_CHAR` meist sinnvoll: Es bricht bevorzugt an Wortgrenzen, kann aber notfalls auch innerhalb langer Zeichenketten umbrechen.

### 5. Zeilenweise oder abschnittsweise rendern

Für Editor-Text ist zeilenweises Rendering oft kontrollierbarer als ein riesiges Layout für den ganzen Text.

Vorteile:

- Seitentrenner sind leicht zu behandeln.
- Vertikaler Überlauf ist einfach prüfbar.
- Man kann später Zeilennummern, Syntaxfarben oder Markierungen ergänzen.
- Man kann harte Zeilenenden respektieren.

Prinzip:

```cpp
double x = settings.leftMarginPoints;
double y = settings.topMarginPoints;
double bottom = settings.pageHeightPoints - settings.bottomMarginPoints;

for (const auto& line : lines) {
    pango_layout_set_text(layout, line.c_str(), -1);

    int width = 0;
    int height = 0;
    pango_layout_get_size(layout, &width, &height);

    double lineHeight = static_cast<double>(height) / PANGO_SCALE;

    if (y + lineHeight > bottom) {
        cairo_show_page(cr);
        y = settings.topMarginPoints;
    }

    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);

    y += lineHeight;
}
```

### 6. Expliziten Seitenumbruch behandeln

Wenn eine logische Seite beendet ist, wird eine neue PDF-Seite begonnen:

```cpp
cairo_show_page(cr);
```

Aber: Am Ende des gesamten Dokuments sollte keine unnötige leere Seite erzeugt werden.

Daher:

```cpp
if (!isLastLogicalPage) {
    cairo_show_page(cr);
}
```

### 7. Ressourcen freigeben

```cpp
pango_font_description_free(font);
g_object_unref(layout);

cairo_destroy(cr);
cairo_surface_destroy(surface);
```

## Seitentrenner-Literal

Empfohlene Varianten:

| Literal | Bedeutung |
|---|---|
| `\f` | klassischer Form Feed |
| `---PAGE---` | gut sichtbar in Textdateien |
| `#PAGE#` | kurz und eindeutig |
| eigenes Setting | flexibel für MR/MEMAC |

Für einen Editor ist `\f` technisch sauber, aber visuell unsichtbar. Ein sichtbares Literal wie `---PAGE---` ist leichter zu debuggen.

Empfehlung:

```cpp
settings.pageSeparatorLiteral = "\f";
```

als Default, aber im Setup konfigurierbar machen.

## Rechter Print-Margin

Es gibt zwei sinnvolle Interpretationen.

### Variante A: Physischer rechter Seitenrand

Der rechte Rand ist ein Abstand in Punkten/mm/inch zur Papierkante.

Vorteil:

- einfach
- PDF-typisch
- unabhängig von Fontmetriken

Nachteil:

- entspricht nicht unbedingt dem Editor-Konzept „Spalte 120“

### Variante B: Print-Margin als Spaltenbegrenzung

Der rechte Print-Margin entspricht einer Editor-Spalte, z. B. 80, 100 oder 120.

Vorteil:

- entspricht typischer Editor-Logik
- sinnvoll für Code
- passt zu Monospace-Fonts

Nachteil:

- braucht Fontmetrik bzw. Zeichenbreitenschätzung

Empfehlung für MR:

```text
Beides unterstützen.
Physischer Rand begrenzt immer.
Print-Margin-Spalte begrenzt zusätzlich, wenn aktiviert.
```

Effektive Breite:

```cpp
effectiveWidth = min(
    physicalUsableWidth,
    printMarginColumns * monospaceCharacterWidth
);
```

## Fonts

Für Code-Export:

- `DejaVu Sans Mono`
- `Noto Sans Mono`
- `JetBrains Mono`
- `Cascadia Code`

Empfehlung als robuster Default:

```text
DejaVu Sans Mono 10
```

Später kann das über Settings konfigurierbar gemacht werden.

## Fehlerbehandlung

Mindestens prüfen:

- Ausgabepfad leer?
- PDF-Surface erzeugbar?
- Fontbeschreibung gültig?
- Eingabetext gültiges UTF-8?
- Seitentrenner-Literal leer?
- Seitenbreite und Ränder plausibel?
- effektive Layoutbreite > 0?
- effektive Layouthöhe > 0?

Wenn das Seitentrenner-Literal leer ist, darf nicht gesplittet werden. Ein leeres Literal wäre ein Konfigurationsfehler.

## UTF-8-Validierung

Pango erwartet UTF-8. Vor dem Export sollte der Text validiert werden.

Mögliche Optionen:

- GLib-Funktion `g_utf8_validate`
- eigene Validierung
- ungültige Sequenzen ersetzen

Pragmatische Empfehlung:

```text
Ungültige UTF-8-Sequenzen nicht stillschweigend durchreichen.
Entweder Export abbrechen oder sichtbar durch U+FFFD ersetzen.
```

## Spätere Erweiterungen

Die Architektur lässt spätere Erweiterungen zu:

- Zeilennummern
- Header/Footer
- Dateiname im Kopf
- Seitenzahlen
- Syntaxfarben
- sichtbare Whitespace-Zeichen
- Hervorhebung des Print-Margins
- Export ausgewählter Bereiche
- Export mit/ohne Hard-Wrap
- Export mit aktuellem MR-Farbschema
- PDF-Metadaten

## Minimaler Implementierungsplan

### Phase 1: Plain-Text-Export

- Klasse `MRPdfTextExporter` anlegen
- Settings-Struktur definieren
- Cairo PDF-Surface erzeugen
- PangoLayout erzeugen
- UTF-8-Text zeilenweise rendern
- Seitenumbruch bei vertikalem Überlauf
- Seitentrenner-Literal respektieren

### Phase 2: Print-Margin sauber machen

- physische Ränder implementieren
- optionalen Spalten-Print-Margin implementieren
- Monospace-Zeichenbreite aus Pango-Fontmetrik bestimmen
- `effectiveWidth` daraus berechnen

### Phase 3: MR-Integration

- Export-Menüpunkt ergänzen
- Settings aus zentralem MR-Settings-System lesen
- keine separate Settings-Datei einführen
- Defaults in bestehendes Settingsmodell integrieren
- Ausgabe über Message-Line melden

### Phase 4: Erweiterungen

- Header/Footer
- Seitenzahlen
- Dateiname
- optional Syntaxfarben
- optional Zeilennummern

## Makefile / pkg-config

Benötigte Compiler-/Linkerflags:

```zsh
pkg-config --cflags --libs pangocairo cairo
```

Beispiel im Makefile:

```make
PDF_EXPORT_CFLAGS := $(shell pkg-config --cflags pangocairo cairo)
PDF_EXPORT_LIBS   := $(shell pkg-config --libs pangocairo cairo)
```

Bei clang++:

```zsh
clang++ -std=c++20 main.cpp -o pdf-export-test \
  $(pkg-config --cflags --libs pangocairo cairo)
```

## Empfehlung für die konkrete Umsetzung

Für MR würde ich keine große PDF-Abstraktion einführen.

Sinnvoll ist eine kleine, klar abgegrenzte Komponente:

```text
app/export/MRPdfTextExporter.hpp
app/export/MRPdfTextExporter.cpp
```

Diese Komponente sollte nur zuständig sein für:

- UTF-8-Text entgegennehmen
- Settings entgegennehmen
- PDF schreiben
- Fehlerstatus zurückgeben

Nicht zuständig sein sollte sie für:

- Dialoglogik
- MRSETUP-Serialisierung
- Dateiauswahl
- Message-Line-Ausgabe
- Editor-State-Abfragen

Diese Trennung hält die Komponente testbar und verhindert, dass UI- oder Settings-Logik in den Exporter wandert.

## Kurzfazit

Empfohlener Stack:

```text
C++20
Pango
PangoCairo
Cairo PDF Surface
```

Nicht empfohlen als erste Wahl:

```text
libharu
PoDoFo
MuPDF
Qt PDF
```

Begründung:

```text
Pango + Cairo löst genau das Kernproblem:
UTF-8-Text sauber layouten und als mehrseitiges PDF ausgeben.
```
