# TVision Git-Subtree Plan

Stand: 2026-03-29

## Ziel

`tvision/` soll so geführt werden, dass:

- Upstream-Importe von `magiblot/tvision` nachvollziehbar bleiben
- lokale TVision-Patches klein und reviewbar bleiben
- Build-Artefakte die TVision-Historie nicht verschmutzen

## Ausgangslage

Aktuell ist `tvision/` ein vendorter Quellbaum ohne eigenes `.git` und ohne `.gitmodules`.

Das bedeutet:

- kein sauberer Upstream-Bezug im Git-Modell
- lokale und generierte Änderungen vermischen sich leicht
- Upstream-Abgleich ist unnötig mühsam

## Zielbild

`tvision/` wird als `git subtree` aus `magiblot/tvision` geführt.

Empfohlene Konvention:

- Upstream-Quelle: `magiblot/tvision`
- Prefix: `tvision`
- lokale Anpassungen nur in separaten, kleinen Commits
- Upstream-fähige Änderungen zusätzlich als Patch-Dateien unter `patches/`

## Migrationsschritte

### Schritt 1: Arbeitsbaum disziplinieren

Vorbedingung:

- Build-Artefakte unter `tvision/build*` dürfen nicht als Source-Wahrheit behandelt werden
- TVision-Source-Änderungen müssen separat sichtbar sein

Empfehlung:

- `.gitignore` für TVision-Build-Verzeichnisse schärfen
- interne Team-Regel: Upstream-Vergleiche nur gegen `tvision/include` und `tvision/source`

### Schritt 2: Upstream als zusätzliche Git-Remote definieren

Beispiel:

```bash
git remote add tvision-upstream https://github.com/magiblot/tvision.git
git fetch tvision-upstream
```

Zweck:

- klarer Upstream-Bezug
- reproduzierbarer Importpunkt

### Schritt 3: Importstrategie festlegen

Es gibt zwei praktikable Varianten:

1. Einmalige saubere Neuaufnahme per `git subtree add`
2. Bestehenden Baum historisch akzeptieren und künftige Updates per `subtree pull` führen

Empfehlung:

Variante `2`, wenn Sie die aktuelle Repo-Historie erhalten wollen und kurzfristig keinen größeren Strukturumbau wünschen.

## Künftiger Workflow

### Upstream einziehen

```bash
git fetch tvision-upstream
git subtree pull --prefix=tvision tvision-upstream master --squash
```

### Lokale TVision-Fixes einpflegen

Regel:

- jeder TVision-Fix ein eigener Commit
- jeder Upstream-Kandidat zusätzlich als Patch-Datei dokumentiert

Beispiele:

- `tvision: add missing include in codepage.h`
- `tvision: guard TCommandSet against out-of-range command ids`

### Konfliktbehandlung

Bei Konflikten immer in dieser Reihenfolge prüfen:

1. Ist die Änderung bereits in Upstream angekommen?
2. Ist die lokale Änderung nur Build-/Format-Rauschen?
3. Ist die Änderung wirklich projektspezifisch?
4. Ist sie upstream-fähig und sollte aus dem lokalen Stack verschwinden?

## Commit-Disziplin

Empfohlene Commit-Typen:

1. `subtree import`: reine Upstream-Einzüge
2. `tvision local patch`: echte lokale TVision-Source-Fixes
3. `mr integration`: Änderungen nur auf `mr`-Seite

Diese Typen nicht mischen.

## Empfehlung

Kurzfristig:

1. TVision-Build-Artefakte konzeptionell aus der Upstream-Betrachtung ausschließen
2. lokale TVision-Änderungen klein halten
3. Upstream-Kandidaten separat dokumentieren

Mittelfristig:

4. `tvision-upstream` als Remote anlegen
5. künftige Updates mit `git subtree pull --prefix=tvision ... --squash` fahren

Langfristig:

6. lokale TVision-Abweichungen möglichst gegen null drücken
7. alles Generische entweder upstreamen oder bewusst als Patch-Queue dokumentieren
