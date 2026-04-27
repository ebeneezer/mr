# Sitzungskontext Keymapping / WordStar / Window List

Stand: 2026-04-26, abends

## Kurzfassung

- Der Key Manager, die Persistenz in `settings.mrmac`, das `WORDSTAR`-Profil und der Laufzeit-Resolver sind implementiert.
- Die WordStar-Präfixe funktionieren inzwischen weitgehend korrekt.
- Die noch offene Hauptbaustelle ist die `Window List` beim `unhide` eines Fensters: Das Ziel wird formal aktiviert, landet aber sichtbar unter einem anderen Fenster in der Z-Reihenfolge.

## Aktueller fachlicher Zustand

### Keymapping / WordStar

Bestätigt funktionierend:

- `^Q^R`
- `^Q^C`
- `^K^B`
- `^K^K`
- `^K^C`
- `^K<SPACE>` wird im Resolver korrekt erkannt und gematcht

Wichtiger Befund:

- Das frühere Problem war **nicht** der Trie selbst.
- Ursache war zuerst ein globaler Pending-State über konkurrierende Kontexte.
- Danach funkte zusätzlich die Menubar mit eigenen Shortcuts dazwischen, insbesondere bei `Ctrl+R`.

Bereits umgesetzt:

- Resolver hält Pending-State jetzt **pro Kontext** statt global.
- Menubar hält sich bei laufender `EDIT`-Präfixkette zurück.
- `Ctrl+A..Z`-TTY-Normalisierung wurde auf Laufzeitpfad und Capture-Pfad gezogen.

Dateien mit relevanten Änderungen:

- `keymap/MRKeymapResolver.hpp`
- `keymap/MRKeymapResolver.cpp`
- `ui/MRMenuBar.cpp`
- `ui/MRWindowSupport.cpp`
- `app/MREditorApp.cpp`

### Window List / Nichtmodalität / Z-Ordnung

Wunsch des Users:

- Nichtmodale Dialoge sollen grundsätzlich bevorzugt werden.
- Die Problemlösung darf **nicht** darin bestehen, alles modal zu machen.
- Der Bug muss fachlich korrekt lokalisiert und behoben werden.

Bestätigter aktueller Befund:

- `Window List` ist modeless.
- `unhide 'MR LOG'` läuft inzwischen über deferred activation.
- Der Deferred-Pfad wird laut Log korrekt ausgeführt.
- Danach ist `MR LOG` formal `visible=1 selected=1`.
- Trotzdem liegt sichtbar noch ein anderes Textfenster darüber.
- Die `Window List` bleibt sichtbar, aber ist danach `selected=0`.

Schlussfolgerung:

- Das ist nicht mehr primär ein Fokusproblem.
- Es ist sehr wahrscheinlich ein **Z-Order-/Front-Bring-Problem** beim Aktivieren bereits existierender Fenster.

### Save in Window List

Bereits geklärt:

- `Save` für das Logfenster funktioniert inzwischen.
- Früher war die read-only-Ablehnung fachlich falsch.
- Jetzt wird in den bekannten Logpfad geschrieben.
- Sichtbare Rückmeldung erfolgt über Messageline.

## Relevante Logbefunde

Aus `/tmp/mr.log`:

```text
KEYDBG keymap context=edit rawCode=0x0011 rawMods=0x0208 token=<Ctrl+Q> result=pending sequence=<Ctrl+Q> target=
KEYDBG keymap context=edit rawCode=0x0012 rawMods=0x0208 token=<Ctrl+R> result=matched sequence=<Ctrl+Q><Ctrl+R> target=MRMAC_CURSOR_TOP_OF_FILE
KEYDBG keymap context=edit rawCode=0x0002 rawMods=0x0208 token=<Ctrl+B> result=matched sequence=<Ctrl+K><Ctrl+B> target=MRMAC_BLOCK_SET_BEGIN
KEYDBG keymap context=edit rawCode=0x0020 rawMods=0x0200 token=<Space> result=matched sequence=<Ctrl+K><Space> target=MRMAC_BLOCK_CLEAR
```

Window-List-/Z-Befund:

```text
Window List handleHide unhide 'MR LOG' visible=0 selected=0 hidden=1
mrScheduleWindowActivation target='MR LOG' visible=0 selected=0 hidden=0
mrDispatchDeferredWindowActivation before target='MR LOG' visible=0 selected=0 hidden=0
mrActivateEditWindow before target='MR LOG' visible=0 selected=0 current=dialog('WINDOW LIST') visible=1 selected=1
mrActivateEditWindow after target='MR LOG' visible=1 selected=1 current=edit('MR LOG') visible=1 selected=1
mrDispatchDeferredWindowActivation after target='MR LOG' visible=1 selected=1 hidden=0
Window List after toggle visible=1 selected=0 focus=1
```

Interpretation:

- TVision sieht `MR LOG` nach der Aktivierung als aktuelles Editfenster.
- Sichtbar liegt aber trotzdem noch das andere Textfenster oben.
- Der nächste Reparaturzug sollte daher die **Art der Aktivierung** ändern, nicht wieder den Resolver anfassen.

## Letzter empfohlener nächster Zug

Empfehlung für die nächste Session:

1. `mrActivateEditWindow(...)` von `deskTop->setCurrent(win, TView::normalSelect)` auf einen TVision-treueren Front-Bring-Pfad umstellen, zuerst mit `win->select()`.
   Begründung:
   - kleinster sauberer Eingriff
   - hohe Plausibilität, dass `setCurrent(...)` Fokus setzt, aber Z-Ordnung nicht robust genug mitzieht
   - passt zur Präferenz für nichtmodale Dialoge

2. Falls das nicht reicht:
   - gezielt prüfen, ob ein zusätzlicher Front-Bring-Schritt nötig ist
   - aber erst nach dem Versuch mit `select()`

Nicht empfohlen:

- Fenster neu erzeugen / aus Desktop entfernen und neu einhängen
- Dialoge modal machen
- allgemeines Refactoring ohne vorherigen lokalen Reparaturversuch

## Weitere kleine bekannte Punkte

- `^K<SPACE>` ist **im Resolver korrekt**. Falls der Effekt “nicht sichtbar” bleibt, ist das kein Eingabeproblem mehr, sondern die Semantik der Blockanzeige bzw. des Blockzustands.
- Die `Window List`-Buttons wurden bereits von den alten `<F3>`/`<ENTER>`-Textresten bereinigt.
- Zusätzliche Debug-Logs wurden in `ui/MRWindowSupport.cpp` und `dialogs/MRWindowList.cpp` eingebaut. Für die nächste Repro-Runde weiterhin mit `MR_KEY_DEBUG=1` starten.

## Betroffene Dateien zuletzt

- `keymap/MRKeymapResolver.hpp`
- `keymap/MRKeymapResolver.cpp`
- `ui/MRMenuBar.cpp`
- `ui/MRWindowSupport.cpp`
- `dialogs/MRWindowList.cpp`
- `app/MREditorApp.cpp`

## Empfehlung an die nächste Session

Nicht wieder breit diagnostizieren. Der nächste sachgerechte erste Schritt ist:

- **Z-Order-Fix in `mrActivateEditWindow(...)`**

Erst danach erneut:

- `Window List` -> `unhide MR LOG`
- prüfen, ob `MR LOG` jetzt sichtbar nach vorne kommt
- nur falls nicht, weiter in die TVision-Z-Reihenfolge hineinbohren
