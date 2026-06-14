# FE Profile Conformance QS

## Anlass

Die Regression um das C-Profil und den Format-Ruler hat gezeigt, dass punktuelle
Tests fuer einzelne Dateitypen nicht ausreichen. Filename-Extension-Profile
bilden einen Vertrag zwischen Settings, Serialisierung, VM-Apply, Editorzustand
und sichtbarem Redraw. Dieser Vertrag muss als Ganzes geprueft werden.

Ziel ist nicht ein einzelner C-spezifischer Regressionstest, sondern eine
Conformance-Suite fuer FE-Profile.

## Betroffener Vertrag

Ein FE-Profil gilt als korrekt verarbeitet, wenn alle folgenden Stationen
konsistent sind:

1. Profilparameter ist als profilfaehig deklariert.
2. `MRFEPROFILE('SET', ...)` liest den Parameter.
3. Das passende Override-Bit wird gesetzt.
4. `effectiveEditSetupSettingsForPath(...)` merged globale Defaults und
   Profilwerte korrekt.
5. Serialisierung schreibt den Wert wieder als `MRFEPROFILE('SET', ...)`.
6. Re-Apply der serialisierten Settings stellt denselben Runtime-Zustand her.
7. VM-naher Settings-Apply setzt Dirty-State nur bei echten Aenderungen.
8. Ein echter Editor mit persistentem Dateinamen konsumiert die effektiven
   Werte.
9. Redraw-relevante Werte werden sichtbar aktualisiert.

Das gilt fuer alle benannten Setup-Elemente, nicht nur fuer bereits auffaellige
Parameter. Ein Name darf nicht nur im Setup-Dialog, in einer Descriptor-Tabelle
oder als Profil-Token existieren. Der Regcheck muss fuer jeden benannten
globalen und profilbasierten Setup-Token nachweisen:

- der Bootstrap-/Load-Pfad kennt den Token,
- die VM kann den Token anwenden,
- das Runtime-Modell kann den Wert aufnehmen,
- der Serializer kann den Token wieder schreiben,
- ein Roundtrip erzeugt denselben semantischen Zustand.

Fehlt einer dieser Pfade, ist der Token nicht konform. Das gilt ausdruecklich
auch fuer profilbasierte Tokens wie `MRFEPROFILE('SET', ...)`.

## Parameter-Inventar

Quelle fuer die Suite ist nicht eine handgeschriebene Sonderliste, sondern das
Settings-Inventar:

- `MREditSettingDescriptor`
- alle Eintraege mit `profileSupported == true`
- das jeweils zugehoerige Override-Bit
- der kanonische Settings-Key

Die Suite soll rot werden, wenn ein neuer profilfaehiger Parameter eingefuehrt
wird, aber nicht durch Parse, Merge, Serialisierung und Consumer-Pruefung
abgedeckt ist.

Zusaetzlich soll die Suite rot werden, wenn ein benannter Setup-Token im
Inventar auftaucht, aber Bootstrap, VM-Apply oder Serializer ihn nicht kennen.
Das verhindert sichtbare Dialog-/Profiloptionen, die nur scheinbar existieren.

## Pruefebenen

### 1. Default-Fallback

Fall ohne passendes Profil:

- globale `MREditSetupSettings` setzen,
- Datei mit nicht gematchter Extension pruefen,
- kein Profilname darf gemeldet werden,
- effektive Settings muessen exakt den globalen Settings entsprechen.

Damit wird verhindert, dass Profil-Logik globalen Editorzustand heimlich
ueberschreibt.

### 2. Profil-Merge

Fall mit passender Extension:

- globale Settings bewusst auf Gegenwerte setzen,
- Profil mit einer Test-Extension definieren,
- pro Parameter genau einen Override setzen,
- `effectiveEditSetupSettingsForPath(...)` pruefen.

Wichtig ist die Negativseite: Nicht gesetzte Profilfelder duerfen globale Werte
nicht ueberschreiben.

### 3. Parse und Override-Mask

Fuer jeden profilfaehigen Parameter:

- Settings-Quelle mit `MRFEPROFILE('DEFINE', ...)`,
  `MRFEPROFILE('EXT', ...)` und `MRFEPROFILE('SET', ...)` erzeugen,
- ueber den normalen Settings-Apply-Pfad laden,
- Profil existiert,
- Extension ist normalisiert vorhanden,
- erwartetes Override-Bit ist gesetzt,
- Wert ist kanonisch normalisiert.
- Bootstrap-/Load-Pfad, VM-Apply und Serializer muessen denselben Token-Namen
  kennen.

### 4. Serialisierung und Roundtrip

Aus Runtime-Modell:

- Settings-Macro bauen,
- Runtime zuruecksetzen,
- Macro wieder anwenden,
- Profilanzahl, IDs, Namen, Extensions, Override-Masks und effektive Werte
  vergleichen.

Der Test muss sowohl direkte Runtime-API-Seeding-Pfade als auch
`MRFEPROFILE`-Quelle abdecken, weil beide Pfade in der Praxis relevant sind.

### 5. VM Apply und Dirty-State

Die Suite soll den VM-nahen Apply-Pfad separat pruefen:

- frische Runtime,
- `MRFEPROFILE`-Quelle anwenden,
- Dirty-State wird bei echter Aenderung gesetzt,
- erneutes Anwenden identischer Daten darf keinen falschen Dirty-Impuls
  erzeugen, sofern der bestehende Settings-Vertrag das verlangt,
- ungueltige Werte duerfen keine partiellen Profilzustaende hinterlassen.

Dieser Teil ist settings- und VM-nah und braucht vor Implementierung eine
gesonderte Vertragspruefung gegen Settings Runtime, Bootstrap und Persistence.

### 6. Editor-Consumer und Redraw

Nicht jeder Profilparameter ist gleich gut visuell beobachtbar. Die Suite soll
die Parameter in Consumer-Gruppen aufteilen:

- Layout: `FORMAT_RULER`, `LINE_NUMBERS_POSITION`, `MINIMAP_POSITION`,
  `CODE_FOLDING_POSITION`, `GUTTERS`
- Syntax: `CODE_LANGUAGE`, `CODE_COLORING`, `CODE_FOLDING`
- Editing: `TAB_SIZE`, `TAB_EXPAND`, `DISPLAY_TABS`, `DEFAULT_MODE`,
  `INDENT_STYLE`, `WORD_WRAP`
- Save/IO: `TRUNCATE_SPACES`, `EOF_CTRL_Z`, `EOF_CR_LF`, `FILE_TYPE`,
  `BACKUP_FILES`
- Blocks: `PERSISTENT_BLOCKS`, `BLOCK_MOVE`
- Paths/Macros: `POST_LOAD_MACRO`, `PRE_SAVE_MACRO`, `DEFAULT_PATH`

Fuer sichtbare Parameter soll ein echter `MREditWindow` mit persistentem
Dateinamen verwendet werden. Danach muessen Refresh- und Redraw-Pfade pruefbar
sein, zum Beispiel ueber Viewport-Geometrie, Syntaxsprache, Cursor-/Mode-Zustand
oder vorhandene oeffentliche Editor-Abfragen.

Wenn ein Parameter keinen stabilen oeffentlichen Consumer hat, soll der Test das
explizit dokumentieren und wenigstens Parse, Merge, Serialisierung und
Roundtrip absichern.

## Sprachen und Profile

Der bestehende C-Test ist nur ein Vertreter. Die Conformance-Suite soll
mindestens alle von `CODE_LANGUAGE` unterstuetzten Sprachen ueber ein
deterministisches Profilraster pruefen:

- `NONE`
- `AUTO`
- `C`
- `CPP`
- `PYTHON`
- `JAVASCRIPT`
- `TYPESCRIPT`
- `TSX`
- `BASH`
- `ZSH`
- `FISH`
- `JSON`
- `YAML`
- `XML`
- `PERL`
- `SWIFT`
- `RUST`
- `GO`
- `PASCAL`
- `SYSTEMD`
- `MAKE`
- `MRMAC`
- `MARKDOWN`
- `KOTLIN`
- `CSHARP`

Dabei ist zu unterscheiden:

- `CODE_LANGUAGE` als Settings-Wert,
- erkannte `MRSyntaxLanguage`,
- LSP-`languageId`,
- sichtbarer Sprachmarker im Fensterrand.

Diese Ebenen duerfen nicht stillschweigend als identisch behandelt werden.

## Abgrenzung zu LSP

LSP darf von FE-Profilen profitieren, aber FE-Profile gehoeren nicht zum LSP
selbst. Fuer LSP sind sie Eingabe in den Workspace-/Editor-Snapshot:

- Dateipfad und Extension bestimmen das effektive Profil.
- Effektive Syntaxsprache bestimmt LSP-`languageId`.
- Profilwerte duerfen nicht im LSP-Server autoritativ werden.
- Der Server bekommt nur den Protokollspiegel.

Die FE-Profile-Conformance-Suite schuetzt damit auch LSP indirekt, ohne LSP
selbst zum Settings-Besitzer zu machen.

## Implementierungsstrategie

Die Suite sollte stufenweise entstehen:

1. Inventar-Test fuer alle `profileSupported` Parameter.
2. Parse/Merge/Serialize/Roundtrip fuer alle Parameter.
3. Default-Fallback und Profil-Fallback als Laufzeittests.
4. Sichtbare Editor-Consumer fuer Layout und Syntax.
5. Dirty-State und VM-Apply.
6. Redraw-nahe Tests nur dort, wo stabile oeffentliche Beobachtung moeglich ist.

Keine neue generische Testframework-Schicht ist noetig. Die vorhandene
`regression/mr-regression-checks.cpp` kann erweitert werden, solange die Checks
konkret bleiben und keine Mini-Frameworks entstehen.

## Offene Entscheidungen

- Welche Parameter gelten als visuell beobachtbar genug fuer Editor-Consumer-
  Tests?
- Muss Dirty-State fuer identischen Re-Apply strikt unveraendert bleiben, oder
  ist nur ein sauberer finaler Runtime-Zustand Vertragsbestandteil?
- Sollen FE-Profil-Conformance-Checks in Core laufen oder teilweise nur in Full?
- Soll ein einzelner roter Conformance-Check alle Parameter listen, oder sollen
  mehrere thematische Checks verwendet werden?

## Naechster Schritt

Morgen zuerst das Parameter-Inventar festziehen. Danach die erste Tranche bauen:
Default-Fallback, Profil-Merge und Roundtrip fuer alle profilfaehigen
Parameter, noch ohne Redraw-nahe Tests.
