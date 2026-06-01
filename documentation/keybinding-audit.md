# Keybinding Audit

Arbeitsstand fuer die Neuordnung der Default-Tastaturkuerzel.

Legende:

| Prio | Bedeutung |
|---|---|
| P0 | staendig beim Editieren, blind erreichbar |
| P1 | haeufig, kurzer Shortcut sinnvoll |
| P2 | gelegentlich, Menue plus optionaler Shortcut |
| P3 | Setup/Administration, Menue reicht |
| P4 | gefaehrlich/destruktiv, bewusst weniger leicht |

## Globale Menue-Mnemonics

| Prio | Kuerzel | Belegt durch | Kontext | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 2 | Alt-F | File-Menue | global Menue | nein | Menue-Mnemonic, potentiell teuer |
| 2 | Alt-E | Edit-Menue | global Menue | nein |  |
| 2 | Alt-W | Window-Menue | global Menue | nein | auch editorisch relevant |
| 2 | Alt-B | Block-Menue | global Menue | nein | konkurriert mit Block-Arbeit |
| 2 | Alt-S | Search-Menue | global Menue | nein | Search ist P0/P1-Kandidat |
| 2 | Alt-T | Text-Menue | global Menue | nein |  |
| 2 | Alt-O | Other-Menue | global Menue | nein | vermutlich niedrige Prio |
| 1 | Alt-M | Toggle block marking | global/editor | `BLOCK_TOGGLE_MARKING` | Macro-Menue-Mnemonic freigegeben |
| 4 | Alt-H | Help-Menue | global Menue | nein | vermutlich niedrige Prio |

## Statusline / App-Hardcode

| Prio | Kuerzel | Befehl | Kontext | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 3 | F1 | Help | baseline/start | nein | im Editor aktuell Record |
| 4 | F10 | Menu | baseline/start | nein | TVision-Konvention |
| 3 | Alt-F10 | Record macro | global/app | `MR_MACRO_TOGGLE_RECORDING` | direkt in `MREditorApp` |
| 1 | Alt-X | Exit | global/app/menu | teils `MR_EXIT_DIRTY_SAVE_ALL` | Alt-X bleibt historisch plausibel |
| 1 | F9 | Build current file | editor/app hardcode | `MR_BUILD_CURRENT_FILE` | doppelt: Menue und direkte App-Abfrage |
| 1 | Ctrl-Alt-Z | Redo | app direct fallback | `MRMAC_REDO_LAST_UNDO` | hardcoded Sonderfall |

## Startzustand: Kein Fenster

| Prio | Kuerzel | Befehl | Quelle | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 3 | F1 | Help contents | Statusline | nein |  |
| 1 | F2 | Load | Statusline/Menu overlay | nein | kein Action-Katalog |
| 1 | F3 | Open | Statusline/Menu | nein |  |
| 3 | F4 | Acquire | Statusline/Menu overlay | nein |  |
| 3 | F5 | Multi-file search | Statusline/Menu overlay | `MRMAC_SEARCH_MULTI_FILE` |  |
| 2 | F6 | Open window | Statusline/Menu overlay | `MR_WINDOW_OPEN` |  |
| 3 | F7 | Multi-file search/replace | Statusline/Menu overlay | `MR_SEARCH_MULTI_FILE_REPLACE` |  |
| 2 | F8 | Open live log | Statusline/Menu overlay | nein |  |
| 4 | F9 | Open journal | Statusline/Menu overlay | nein |  |
| 4 | F10 | Menu | Statusline | nein |  |
| 2 | F11 | Setup UI | Statusline/Menu overlay | nein | vermutlich P3 |
| 4 | F12 | Exit | Statusline | `MR_EXIT_DIRTY_SAVE_ALL` aehnlich | Alt-X zusaetzlich |

## Editorzustand: Fenster Geoeffnet

| Prio | Kuerzel | Befehl | Quelle | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 3 | F1 | Record macro toggle | Statusline/App direct | `MR_MACRO_TOGGLE_RECORDING` | Konflikt mit Help bewusst |
| 1 | F2 | Save | Statusline/Menu | `MRMAC_FILE_SAVE` | P0/P1 |
| 2 | F3 | Load block | Statusline/Menu overlay | `MR_LOAD_BLOCK_FROM_FILE` |  |
| 2 | F4 | Save block | Statusline/Menu overlay | `MR_SAVE_BLOCK_TO_FILE` |  |
| 3 | F5 | Cascade | Statusline/Menu overlay | `MR_WINDOW_CASCADE` |  |
| 3 | F6 | Tile | Statusline/Menu overlay | `MR_WINDOW_TILE` |  |
| 2 | F7 | Split vertical | Statusline/Menu overlay | `MR_WINDOW_SPLIT_VERTICAL` |  |
| 2 | F8 | Split horizontal | Statusline/Menu overlay | `MR_WINDOW_SPLIT_HORIZONTAL` |  |

Gilt fuer per `Window/Open` erzeugte leere Editorfenster ebenso wie fuer per `File/Load` geoeffnete Dateien.

## File / Edit

| Prio | Kuerzel | Befehl | Kontext | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 2 | F3 | Open | global/menu | nein | global sichtbar, aber Start-F3 auch Open |
| 1 | F2 | Save | global/menu | `MRMAC_FILE_SAVE` | zentral |
| 2 | Ctrl-F2 | Save as | global/menu | nein |  |
| 3 | Alt-X | Exit | global/menu | `MR_EXIT_DIRTY_SAVE_ALL` aehnlich |  |
| 1 | Ctrl-Z | Undo | edit/menu | `MRMAC_UNDO` | P0 |
| 1 | Ctrl-Alt-Z | Redo | edit/menu/direct | `MRMAC_REDO_LAST_UNDO` | P0/P1 |
| 1 | Ctrl-Ins | Cut to buffer | edit/menu | `MRMAC_BLOCK_MOVE_TO_BUFFER` | Terminologie pruefen |
| 1 | Ctrl-Del | Append to buffer | edit/menu | `MRMAC_BLOCK_APPEND_TO_BUFFER` |  |
| 1 | Shift-Ins | Paste from buffer | edit/menu | `MRMAC_BLOCK_COPY_FROM_BUFFER` |  |

## Block

| Prio | Kuerzel | Befehl | Kontext | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 1 | F7 | Mark lines / End marking toggle | editor/menu/direct | teilweise nein | Built-in direct; `MRMAC_BLOCK_SET_BEGIN` existiert, aber nicht "line mark" |
| 1 | Shift-F7 | Mark columns | editor/menu/direct | `MRMAC_BLOCK_SET_COLUMN_BEGIN` | P0/P1 |
| 1 | Ctrl-F7 | Mark stream | editor/menu/direct | `MRMAC_BLOCK_MARK_STREAM` | P0/P1 |
| 2 | Ctrl-F9 | Turn marking off | editor/menu/direct | `MRMAC_BLOCK_CLEAR` | P0, aber CtrlF9 evtl. schwer |
| 1 | F8 | Copy block | block/menu | `MRMAC_BLOCK_COPY` | bleibt Default-Kuerzel |
| 1 | Shift-F8 | Move block | block/menu | `MRMAC_BLOCK_MOVE` | bleibt Default-Kuerzel |
| 1 | Ctrl-F8 | Delete block | block/menu | `MRMAC_BLOCK_DELETE` |  |
| 2 | Shift-F2 | Save block | block/menu | `MR_SAVE_BLOCK_TO_FILE` |  |
| 1 | Alt-F3 | Indent block | block/menu | `MRMAC_BLOCK_INDENT` |  |
| 1 | Alt-F2 | Undent block | block/menu | `MRMAC_BLOCK_UNDENT` |  |
| 1 | Alt-F8 | Window copy | block/menu | `MRMAC_BLOCK_COPY_INTERWINDOW` |  |
| 1 | Alt-F7 | Window move | block/menu | `MRMAC_BLOCK_MOVE_INTERWINDOW` |  |
| 2 | none | Load block | block/menu | `MR_LOAD_BLOCK_FROM_FILE` | derzeit nur Editor-F3-Kontext |

## Search / Text / Build

| Prio | Kuerzel | Befehl | Kontext | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 1 | F5 | Search text | search/menu | `MRMAC_SEARCH_FORWARD` | Konflikt Start-MFS / Editor-Cascade |
| 1 | Shift-F5 | Search replace | search/menu | `MRMAC_SEARCH_REPLACE` |  |
| 2 | Ctrl-F5 | Repeat search | search/menu | `MRMAC_SEARCH_REPEAT_LAST` | Match brace hat kein Default-Kuerzel mehr |
| 3 | Alt-Shift-F | Multi-file search | search/menu | `MRMAC_SEARCH_MULTI_FILE` |  |
| 3 | Alt-Shift-G | List files from search | search/menu | `MRMAC_SEARCH_LIST_MATCHED_FILES` |  |
| 3 | Alt-Shift-R | MFS replace | search/menu | `MULTI_FILE_SEARCH_REPLACE` |  |
| 3 | F4 | Push marker | search/text/menu | `MRMAC_MARK_PUSH_POSITION` | Konflikt Start-Acquire / Editor-SaveBlk |
| 3 | Shift-F4 | Get marker | search/text/menu | `MRMAC_MARK_POP_POSITION` |  |
| 2 | Alt-F5 | Goto line | search/menu | `MRMAC_CURSOR_GOTO_LINE` |  |
| 4 | Alt-R | Reformat paragraph | text/menu | `MR_TEXT_REFORMAT_PARAGRAPH` | evtl. Terminal-ok |
| 1 | F9 | Build current file | other/menu/direct/keymap | `MR_BUILD_CURRENT_FILE` | zentral fuer Coder |
| 3 | none | Next compiler error | other/menu | `MR_COMPILER_PROBLEMS_NEXT` | kein Default-Kuerzel; F8 bleibt Block Copy |
| 3 | none | Prev compiler error | other/menu | `MR_COMPILER_PROBLEMS_PREVIOUS` | kein Default-Kuerzel; Shift-F8 bleibt Block Move |
| 1 | Alt-P | Match parenthesis | other/menu/keymap | `MR_MATCH_PARENTHESIS` | springt zwischen (), [] und {} |

## Window / Desktop

| Prio | Kuerzel | Befehl | Kontext | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 2 | Ctrl-F6 | Window list | window/menu | `MR_WINDOW_LIST` |  |
| 1 | F6 | Next window | window/menu | `MR_WINDOW_NEXT` | Konflikt Start-open-window / Editor-tile |
| 1 | Shift-F6 | Previous window | window/menu | `MR_WINDOW_PREVIOUS` |  |
| 3 | Alt-F6 | Zoom | window/menu | `MR_WINDOW_ZOOM` |  |
| 1 | Ctrl-F11 | Move viewport left | window organize/menu | `MR_DESKTOP_VIEWPORT_LEFT` | neu, testen |
| 1 | Ctrl-F12 | Move viewport right | window organize/menu | `MR_DESKTOP_VIEWPORT_RIGHT` | neu, testen |
| 1 | Shift-F11 | Move window left desktop | window organize/menu | `MR_DESKTOP_MOVE_WINDOW_LEFT` | neu, testen |
| 1 | Shift-F12 | Move window right desktop | window organize/menu | `MR_DESKTOP_MOVE_WINDOW_RIGHT` | neu, testen |
| 3 | F5 | Cascade | editor context | `MR_WINDOW_CASCADE` |  |
| 2 | F6 | Tile | editor context | `MR_WINDOW_TILE` |  |
| 1 | F7 | Split vertical | editor context | `MR_WINDOW_SPLIT_VERTICAL` |  |
| 1 | F8 | Split horizontal | editor context | `MR_WINDOW_SPLIT_HORIZONTAL` |  |

## Help / Macro / Setup / Other

| Prio | Kuerzel | Befehl | Kontext | KM/MRMAC | Kommentar |
|---|---|---|---|---|---|
| 3 | Alt-F10 | Record macro | macro/menu/app | `MR_MACRO_TOGGLE_RECORDING` | global hart |
| 4 | F1 | Help contents / keys | help/menu/status | nein | Doppelbelegung im Help-Menue |
| 4 | Shift-F1 | Detailed index | help/menu | nein |  |
| 4 | Alt-F1 | Previous topic | help/menu | nein |  |
| 4 | Alt-A | ASCII table | other/menu | nein |  |
| 3 | none | Emoji table | other/menu | nein |  |
| 3 | none | Macro manager | macro/menu | nein | Macro-UI nicht automatisiert |
| 4 | none | Color/keymap/setup sections | setup/menu | `MR_SETUP_*` | Action-Ziele und MRMAC-Prozeduren ohne Default-Kuerzel |

## Auffaellige Konflikte / Schulden

| Thema | Befund | Konsequenz |
|---|---|---|
| Alt-M | Blockmarkierung toggle | umgesetzt; Macro-Menue hat keine Alt-M-Mnemonic mehr |
| Ctrl-F5 | Repeat search vs Match parenthesis | geloest: Ctrl-F5 bleibt Repeat search, Match parenthesis liegt auf Alt-P |
| F8/Shift-F8 | Block copy/move vs compiler next/prev labels | geloest: F8/Shift-F8 bleiben Block, Compiler-Navigation ohne Default-Kuerzel |
| F7 | Mark lines und End marking | Absichtlich toggle-artig, aber Name/Action differiert |
| Window/Desktop | Keymap/MRMAC Actions vorhanden | keine Default-Hotkeys fuer betriebszustandsuebergreifende Desktop-Aktionen |
| Menues/Help | grossteils keine Actions | wahrscheinlich ok, aber bewusst entscheiden |
| Modifier-Preview | technisch nicht stabil | Option 1 bleibt richtig: Menue-Labels statt Live-Layer |
| Repeat command | Platzhalter ohne definierte Semantik | aus Hotkey-Liste und Menue entfernt; kein Keymap/MRMAC-Dummy |
| Text/Layout und Window/Modify size | Platzhalter ohne erkennbare Semantik | Menueeintraege entfernt; Command-IDs bleiben numerisch stabil |
| Execute program | UI-Menueeintrag entfernt | MRMAC nutzt stattdessen `SUBSHELL(command, timeout_ms)` mit Output-Rueckgabe |

## Nachruest-Kandidaten Fuer Keymap / MRMAC

| Prio | Ziel | Zweck | Status |
|---|---|---|---|
| 1 | `MR_WINDOW_OPEN/NEXT/PREVIOUS/LIST/CLOSE/ZOOM` | Window-Grundsteuerung | Action-Ziele und MRMAC-Prozeduren vorhanden |
| 1 | `MR_WINDOW_CASCADE/TILE/SPLIT_VERTICAL/SPLIT_HORIZONTAL` | Editor-Fensterlayout | Action-Ziele und MRMAC-Prozeduren vorhanden |
| 1 | `MR_DESKTOP_VIEWPORT_LEFT/RIGHT` | VP-Steuerung | Action-Ziele und MRMAC-Prozeduren vorhanden |
| 1 | `MR_DESKTOP_MOVE_WINDOW_LEFT/RIGHT` | Window-Desktop-Move | Action-Ziele und MRMAC-Prozeduren vorhanden |
| 1 | `MR_BLOCK_MARK_LINES` | F7-Line-Block explizit statt implizit | Action-Ziel und MRMAC-Prozedur vorhanden |
| 1 | `MR_BLOCK_TOGGLE_MARKING` | Kandidat fuer Alt-M | Action-Ziel und MRMAC-Prozedur vorhanden |
| 2 | `MR_SEARCH_MULTI_FILE_REPLACE` | MFSAR vollstaendig keymapfaehig | Action-Ziel und MRMAC-Prozedur vorhanden |
| 2 | `MR_MACRO_TOGGLE_RECORDING` | Recorder steuerbar | Action-Ziel und MRMAC-Prozedur vorhanden |
| 1 | `MR_MATCH_PARENTHESIS` | Klammerpartner fuer (), [] und {} anspringen | Action-Ziel, MRMAC-Prozedur und Alt-P-Default vorhanden |
| 3 | `MR_SETUP_*` optional | UI-Automation ohne neue Hotkeys | Action-Ziele und MRMAC-Prozeduren vorhanden |
| 2 | `SUBSHELL(command, timeout_ms)` | Subshell aus MRMAC starten und Output lesen | MRMAC-Intrinsic ohne UI-Hotkey |
