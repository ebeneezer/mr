# Sitzungskontext Keymapping / WordStar / Minimap

## Kurzfazit

Der Keymapping-Zug ist architektonisch weit fortgeschritten und für den WordStar-Pfad praktisch benutzbar. Die größten funktionalen Haken von gestern sind beseitigt:

- Profilmodell, Resolver, Präfixzustand und `MRSETUP(...)`-Serialisierung stehen.
- Externe Keymap-Dateien und `settings.mrmac` sind aneinander angebunden.
- Makroziele über `file^macro` funktionieren jetzt auch mit mehreren Makros pro Datei.
- Der Key Manager markiert ungültige Makro-Bindings sichtbar mit `!`.

Aus meiner Sicht ist die Baustelle **noch nicht fachlich abgeschlossen**, aber nicht mehr im Architekturstadium. Der nächste sinnvolle Zug ist kein weiterer ad-hoc-WordStar-Fix, sondern ein sauberer Audit des kanonischen `MRMAC_*`-Katalogs gegen die Referenz.

## Stand Keymapping

### Erledigt

- `MRMAC_*` vs. `MR_*` sind als getrennte Schichten modelliert.
- Keymap-Profile werden über `KEYMAP_PROFILE`, `KEYMAP_BIND`, `ACTIVE_KEYMAP_PROFILE` serialisiert.
- Externe Keymap-Datei wird zusätzlich über `KEYMAPURI` gemerkt.
- Der Resolver arbeitet präfixbasiert ohne Timeout.
- Alias-Schreibweisen wie `^KB` neben `^K^B` sind im WordStar-Profil eingetragen.
- Makro-Bindings referenzieren jetzt `datei^makro` statt nackter Makronamen.
- Relative Makro-Dateiangaben werden zur Laufzeit auch im konfigurierten `Macro Path` aufgelöst.

### WordStar-spezifisch umgesetzt

- Block-/Cursor-/Datei-/Margin-/Paragraph-Kommandos des bisherigen Entwurfs sind weitgehend umgesetzt.
- `^N` ist derzeit als Testbindung auf `wordstar-extensions.mrmac^clone_line_above` gebunden.
- `wordstar-extensions.mrmac` im Macro Path wird vom Key Manager gefunden und als Makroquelle akzeptiert.

### Noch offen bzw. bewusst nicht als „fertig“ zu werten

- Kein harter Vollständigkeitsnachweis, dass der kanonische `MRMAC_*`-Katalog bereits vollständig gegen die Referenz abgedeckt ist.
- `Emacs`- und `Nano`-Profile aus dem Plan sind noch nicht geliefert.
- Einige UI-Korrekturen wurden zuletzt code-seitig behoben, aber nicht alle im Lauf getestet.

## Wichtige Korrekturen des heutigen Zugs

### Makro-Bindings

- Der Fehlerpfad `clone_line_above -> Macro specification could not be resolved` ist behoben.
- Ursache war nicht das Makro selbst, sondern die Auflösung relativer `*.mrmac`-Dateien außerhalb des aktuellen Working Directory.
- Jetzt wird zusätzlich im konfigurierten `Macro Path` gesucht.

### Key Manager

- Defekte Makro-Bindings werden analog zum Macro Manager mit `!` markiert.
- Die Fehlerursache wird in der Beschreibungs-Spalte verkürzt sichtbar gemacht.
- Der visuelle Fokusverlust des Parent-Dialogs beim modalen Binding/Profile-Edit wurde weiter nach unten auf den fokussierten Unter-View gezogen. Das ist code-seitig korrigiert, sollte aber weiter im UI verifiziert werden.

### Kanonischer Katalog

- `MR_COPY_CHAR_FROM_ABOVE` wurde wieder aus dem allgemeinen Keymap-Kanon entfernt.
- Begründung: zu weit weg von der Referenz, daher nicht als hardcodierte MR-Action im Katalog vertretbar.

## Minimap

### Heutiger Befund

Der aktuelle Testbefund ist außergewöhnlich stark:

- `enwik9`
- ca. `1.3` Millionen Zeilen
- beobachtete Minimap-Renderzeit: etwa `7 ms`
- alle `16` Cores zeigen eine Lastspitze

Wenn dieser Befund reproduzierbar ist, ist das fachlich ein sehr starker Durchbruch.

### Technischer Stand

- Die Minimap lief bereits außerhalb des UI-Threads in der Coprozessor-`MiniMap`-Lane.
- Neu ist die interne Aufteilung des Warmups über `y`-Blöcke.
- Kleine Viewports bleiben seriell, größere Payloads werden über mehrere Worker verteilt.
- Die Piece-Table-Line-Offsets wurden **bewusst nicht** mit derselben Technik parallelisiert, weil deren aktueller Stride-/Checkpoint-Aufbau sequenziell abhängiger ist als der Minimap-Pfad.

### Einordnung

Wenn die `7 ms` belastbar sind, ist die Minimap aus dem Bereich „spürbarer Hot Path“ in den Bereich „sehr aggressiv optimiert“ gerutscht. Der nächste sinnvolle Schritt dort wäre nicht sofort weiterer Umbau, sondern erst:

- Reproduzierbarkeit prüfen
- mehrere Dokumenttypen testen
- grob messen, ab welcher Größe die Parallelisierung kippt bzw. lohnt

## Empfehlung für morgen

### Beste Option

Kanonischen `MRMAC_*`-Katalog gegen die Referenz hart auditieren.

Begründung:

- Das Architekturfundament steht.
- WordStar ist praktisch benutzbar.
- Der größte fachliche Restzweifel liegt jetzt nicht mehr in der Resolvertechnik, sondern in der Frage, ob der Katalog wirklich sauber und vollständig am Kanon hängt.

### Zweite Option

Systematische UI-Verifikation des Key Managers und der WordStar-Bindings.

Begründung:

- sinnvoll, wenn morgen eher Produktstabilisierung statt Katalogarbeit gewünscht ist
- besonders relevant für die noch nicht visuell verifizierten Dialogfokusfälle

### Nicht meine Empfehlung als nächster Hauptzug

Sofort `Emacs`/`Nano`-Profile nachziehen.

Begründung:

- erhöht die Menge, aber nicht die fachliche Sicherheit des Fundaments
- besser erst nach Katalog-Audit

## Meine aktuelle Einschätzung

Wir sind **nicht mehr in der Planungsphase**, sondern in einer späten Integrations- und Konsolidierungsphase des Keymapping-Systems.

Die WordStar-Linie ist bereits so weit, dass der nächste professionelle Schritt ein **Audit- und Abschlusszug** sein sollte, nicht weiteres ungeordnetes Feature-Nachschieben.
