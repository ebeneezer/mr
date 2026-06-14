# Color Theme Conformance QS

## Anlass

Die wiederholten kleinen Regressionen im Farbsystem zeigen, dass Theme-Datei,
Palette-Slots, Renderer und Settings-Persistenz als Vertrag betrachtet werden
muessen. Ein einzelner visueller Test reicht hier nicht aus: Farbcodes koennen
korrekt serialisiert sein und trotzdem im Renderer nicht ankommen, oder beim
Theme-Version-Upgrade korrekt ergaenzt werden und dennoch unnoetig erneut
geschrieben werden.

## Grundsaetze

Theme-Version-Upgrades sind ein bewusstes MR-Design.

Wenn ein altes Theme weniger Farbcodes enthaelt als das aktuelle Compilat
kennt, muss MR die fehlenden Slots mit hardcodierten Defaults auffuellen. Diese
Defaults sind Teil des Kompatibilitaetsvertrags, nicht ein Fehler.

Gleiche Farbwerte sind keine Duplikate. Zwei Elemente duerfen denselben
Attributwert tragen, wenn sie fachlich unterschiedliche UI- oder
Syntaxelemente beschreiben.

Ein echtes Duplikat liegt nur vor, wenn unterschiedliche Namen an derselben
Stelle im Code zur selben Colorierung fuehren und deshalb keinen eigenen
steuerbaren Renderpfad besitzen.

## Betroffener Vertrag

Ein Theme-Slot gilt als konform, wenn alle folgenden Stationen konsistent sind:

1. Der Slot ist in einem `MRColorSetupItem` fachlich benannt.
2. Der Slot besitzt einen stabilen Palette-Index.
3. Der Slot ist in `MRColorSetupSettings` der passenden Gruppe enthalten.
4. Parser akzeptieren die aktuelle und die erlaubten alten Listenlaengen.
5. Fehlende Werte werden aus hardcodierten Defaults ergaenzt.
6. Serialisierung schreibt die aktuelle vollstaendige Listenform.
7. `configuredColorSlotOverride(...)` liefert den konfigurierten Wert.
8. Mindestens ein Consumer verwendet den Slot, oder der Slot ist bewusst als
   Reserve dokumentiert.
9. Ein identischer Re-Apply erzeugt keinen unnoetigen Dirty- oder Write-Impuls.

## Relevante Gruppen

- `WINDOWCOLORS`
- `MENUDIALOGCOLORS`
- `HELPCOLORS`
- `OTHERCOLORS`
- `MINIMAPCOLORS`
- `FILECOMPAREMINIMAPCOLORS`
- `CODECOLORS`
- `FILECOMPARECOLORS`

Jede Gruppe braucht einen Inventarcheck: Anzahl, Namen, Palette-Slots,
aktuelle Serialisierungsform und erlaubte Altformate.

## Upgrade-Verhalten

Der Regressionstest muss mindestens drei Faelle unterscheiden.

### 1. Alte Theme-Version mit fehlenden Slots

Eingabe:

- Theme-Version kleiner als `mrCurrentPersistenceVersion()`.
- Eine oder mehrere Farblisten liegen in einem erlaubten alten Format vor.

Erwartung:

- MR akzeptiert das Theme.
- Fehlende Slots werden mit hardcodierten Defaults aufgefuellt.
- Runtime-Zustand enthaelt vollstaendige Gruppen.
- Eine spaetere Serialisierung schreibt die aktuelle Listenform.
- Die neu hinzugefuegten Werte entsprechen exakt den erwarteten Defaults.

### 2. Aktuelle Theme-Version mit vollstaendigen Slots

Eingabe:

- Theme-Version entspricht dem aktuellen Compilat.
- Alle Farblisten liegen in aktueller Form vor.

Erwartung:

- Kein Upgrade-Flag.
- Kein Theme-Schreibvorgang.
- Kein Dirty-Impuls nur durch Laden oder Re-Apply.
- Runtime-Zustand bleibt bytegenau semantisch gleich.

Das ist der wichtige Schutz gegen "Version passt, aber MR schreibt trotzdem".

### 3. Aktuelle Theme-Version mit inkonsistenter Liste

Eingabe:

- Theme-Version entspricht dem aktuellen Compilat.
- Eine Farbliste ist unvollstaendig oder hat eine falsche Laenge.

Erwartung:

- Der Fehler wird gemeldet.
- Es gibt keinen partiellen Runtime-Zustand.
- Es wird nicht stillschweigend mit Defaults repariert, wenn das Format fuer
  die aktuelle Version vollstaendig sein muss.

## Dupes und Slack

Gleiche Farbwerte sind erlaubt und kein Fehler.

Der Test soll deshalb nicht "Wert A kommt mehrfach vor" melden. Er soll
stattdessen pruefen:

- Fuehren zwei fachlich verschiedene Namen auf denselben Palette-Slot?
- Fuehren zwei fachlich verschiedene Namen im Renderer auf denselben
  `MRSyntaxToken` oder denselben konkreten Zeichenpfad?
- Gibt es Slots, die im Setup sichtbar und serialisiert sind, aber keinen
  Consumer besitzen?

Nur diese Faelle sind relevante Dupes oder Slack.

## Aktueller Audit-Befund fuer CODECOLORS

Aktuell besitzt `CODECOLORS` 15 Eintraege:

- `comments`
- `strings`
- `characters`
- `numbers`
- `keywords`
- `types`
- `directives`
- `functions`
- `builtins`
- `constants`
- `operators`
- `brackets`
- `delimiters`
- `sidekick editor text`
- `sidekick editor highlight`

Der Renderer `MRFileEditor::tokenColor(...)` verwendet zurzeit eigene Pfade
fuer:

- `comments`
- `strings`
- `numbers`
- `keywords`
- `types`
- `directives`
- `constants` ueber `MRSyntaxToken::Key`
- `delimiters`

Die Sidekick-Slots werden separat durch `MRSidekickEditor` verwendet.

Derzeit ohne eigenen Renderpfad:

- `characters`
- `functions`
- `builtins`
- `operators`
- `brackets`

Das ist nur dann akzeptabel, wenn diese Slots als Reserve fuer kuenftige
Syntax-Tokens gelten. Andernfalls sollte das Setup sie nicht als steuerbare
Farben anbieten.

## Base-Palette-Risiko

`extendedAppBasePalette()` muss fuer alle erweiterten Slots eine definierte
Basis besitzen. Wenn neue Slots nur ueber `configuredColorSlotOverride(...)`
funktionieren, aber in der Base-Palette nicht initialisiert sind, kann bei
fehlendem Mapping oder fehlerhaftem Theme uninitialisierter Zustand sichtbar
werden.

Der Regcheck soll daher pruefen:

- alle Slots `1..kMrPaletteMax` sind in der Base-Palette definiert,
- alle erweiterten Slots ab `kMrPaletteCurrentLine` haben einen nachvollzieh-
  baren Default,
- die hardcodierten Defaults aus Upgrade-Fallback und Base-Palette widersprechen
  sich nicht.

## Pruefebenen

### 1. Inventar

- Alle `MRColorSetupGroup`-Gruppen lesen.
- Erwartete Anzahl je Gruppe pruefen.
- Doppelte Palette-Slots innerhalb einer Gruppe melden.
- Doppelte Werte nicht melden.
- Sichtbare Setup-Namen gegen Slots pruefen.

### 2. Parser und Altformate

- Aktuelle Form jeder Gruppe akzeptieren.
- Dokumentierte alte Formen akzeptieren.
- Fehlende Werte mit den erwarteten hardcodierten Defaults auffuellen.
- Falsche Laengen fuer aktuelle Form ablehnen.

### 3. Slot Override

- Pro Gruppe eindeutige Probe-Werte setzen.
- `configuredColorSlotOverride(...)` muss fuer jeden Gruppen-Slot exakt den
  Probe-Wert liefern.
- Aliase wie Dialog-Farbvarianten muessen bewusst und nachvollziehbar auf den
  fachlichen Gruppenslot abgebildet sein.

### 4. Consumer

- Fuer jeden Slot pruefen, ob ein Renderer, Widget oder App-Palette-Pfad ihn
  konsumiert.
- Slots ohne Consumer muessen als Reserve dokumentiert sein.
- Fuer Code-Slots muss der Test gegen `MRSyntaxToken` und
  `MRFileEditor::tokenColor(...)` pruefen, nicht nur gegen die Theme-Liste.

### 5. Serialisierung

- Runtime-Farben setzen.
- Theme-Macro bauen.
- Runtime zuruecksetzen.
- Macro wieder anwenden.
- Alle Gruppen muessen semantisch identisch sein.

### 6. No-Write bei gleicher Version

- Theme-Datei mit aktueller `THEME_VERSION` und aktuellen Listen erzeugen.
- Theme laden.
- Settings/Theme-Persistenzpfad ausloesen, ohne Werte zu aendern.
- Dateiinhalt oder beobachtbarer Write-Counter darf sich nicht aendern.

Wenn es keinen stabilen Write-Counter gibt, kann der erste Test ueber
Dateiinhalt und MTime laufen. Eine robustere zweite Stufe waere ein expliziter
Test-Hook im Persistenzpfad, aber nur nach gesonderter Freigabe.

## Abgrenzung

Dieser Plan beschreibt QS und Vertrag. Er ist kein Freibrief, Theme-
Persistenz, Settings-Bootstrap oder `MRSETUP` umzubauen.

Aenderungen an folgenden Bereichen sind geschuetzt und brauchen vorab einen
separaten Plan:

- Settings Bootstrap
- Settings Persistence
- Theme-Datei-Schreibpfade
- VM-Apply von `THEME_VERSION`, `THEME_RESET` und `*COLORS`
- Color Setup Dialog
- App-/Window-Palette-Aufbau

## Naechster Schritt

Als erste Tranche sollte nur ein Audit-/Conformance-Check entstehen:

1. Gruppeninventar.
2. Slot-Override-Pruefung fuer alle Gruppen.
3. CODECOLORS-Slack explizit melden.
4. Upgrade-Fallback fuer ein altes `CODECOLORS`- und `WINDOWCOLORS`-Format.
5. No-Write-Pruefung bei aktueller Theme-Version.

Erst danach entscheiden, ob Slack-Slots entfernt, als Reserve beschriftet oder
durch neue Syntax-Tokens fachlich aktiviert werden.
