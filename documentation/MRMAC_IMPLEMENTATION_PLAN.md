# MRMAC Implementation Plan

## 1. Analysis of the MEMAC Language Reference

I have analyzed the *ME Macro Language Reference Guide.pdf* to determine the full scope of the MEMAC language. I then compared this against the current MRMAC implementation in `mrmac.c`, `lexer.l`, `parser.y` and `mrvm.cpp`.

### Included Language Constructs
The MEMAC macro language is relatively simple and only contains the following control structures:
- `IF expression THEN ... END`
- `IF expression THEN ... ELSE ... END`
- `WHILE expression DO ... END`
- `CALL label` / `RET`
- `GOTO label`

These control flow constructs are already implemented in the MRMAC parser (`parser.y` / `mrmac.c`) and bytecode interpreter (`mrvm.cpp`). There are no `FOR`, `REPEAT`, or `CASE` structures in the MEMAC language according to the manual.

### Missing Commands (Intrinsic Functions, Procedures, System Variables)
The core missing commands can be grouped into several categories based on their function. I have excluded commands that appear to be OCR errors from the PDF (like 'CALUNA', 'SFO', 'OVR_CURSOR') or that represent DOS specifics not relevant for a modern implementation unless emulated (e.g., `DOS_FILE_`).

The following is an organized list of the missing commands and a recommended phased implementation plan:

## 2. Recommended Implementation Phases

### Phase 1: Core Text and Editor State Operations
These commands form the backbone of basic text editing macros and should be implemented first as they do not require complex UI elements or external system interactions.
* **Text Operations:** `BACK_SPACE`, `DEL_CHARS`, `WORD_WRAP_LINE`
* **Cursor/Position Operations:** `NEXT_PAGE`, `LAST_PAGE`, `GOTO_COL`, `C_ROW`, `PG_LINE`, `C_PAGE`
* **Block Operations:** `BLOCK_STAT`, `BLOCK_LINE1`, `BLOCK_LINE2`, `BLOCK_COL1`, `BLOCK_COL2`
* **Editor State Variables (Read/Write):** `INSERT_MODE`, `AUTO_SAVE`, `BACKUPS`, `UNDO_STAT`, `EMS_STAT`
* **Formatting Variables:** `FORMAT_LINE`, `DEFAULT_FORMAT`, `FORMAT_STAT`, `PRINT_MARGIN`, `WORD_DELIMITS`
* **Tabs/Spacing:** `DISPLAY_TABS`, `EXPAND_TABS`

### Phase 2: String, Input, and Feedback Operations
These operations are used to interact with the user via the message line or retrieve user input.
* **String Input/Display:** `STRING_IN`, `MAKE_MESSAGE`, `WRITE`, `CLR_LINE`
* **Keyboard Input:** `READ_KEY`, `CHECK_KEY`, `KEY1`, `KEY2`, `PUSH_KEY`, `PASS_KEY`, `KEY_IN`, `KEY_RECORD`, `PLAY_KEY_MACRO`
* **System Information:** `TIME`, `DATE`, `VERSION`, `CPU`, `COMSPEC`, `ME_PATH`
* **User Feedback:** `BEEP`, `EXPLOSIONS`, `MESSAGES`
* **Help System:** `HELP`, `RETRIEVE_HELP`

### Phase 3: Turbo Vision UI and Mouse Integration
Since the new MR editor uses Turbo Vision, which has built-in mouse support and powerful UI capabilities, these MEMAC UI commands can be mapped directly to Turbo Vision concepts.
* **Mouse Support:** `MOUSE` (Status variable to enable/disable or check mouse support)
* **Menus:** `BAR_MENU`, `V_MENU`
* **Screen Geometry:** `SCREEN_LENGTH`, `SCREEN_WIDTH`, `WHEREX`, `WHEREY`, `GOTOXY`
* **Box Drawing:** `PUT_BOX`, `KILL_BOX`, `SHADOW_COLOR`, `SHADOW_CHAR`
* **Colors/Attributes:** `TEXT_COLOR`, `BACK_COLOR`, `MENU_COLOR`, `STAT_COLOR`, `ERROR_COLOR`, `WINDOW_ATTR`
* **Function Key Labels:** `PUSH_LABELS`, `POP_LABELS`, `FLABEL`

### Phase 4: Advanced Macros, File System, and Shell
These commands handle more complex interactions, such as executing other macros, interfacing with the operating system, and managing the file system.
* **Macro Execution:** `MACRO_TO_KEY`, `CMD_TO_KEY`, `UNASSIGN_KEY`
* **DOS Shell Emulation (Directory Operations):** `ENTER_DOS_SHELL`, `EXIT_DOS_SHELL`, `DIR`, `DOS_UP`, `DOS_DOWN`, `DOS_PGDN`, `UPDATE_DIR`, `DIR_MASK`, `DIR_ENTRY`, `DIR_NUM`, `DIR_TOTAL`, `DIR_PATH`, `FILE_MARKED`, `SHELL_TO_DOS`
* **File Operations:** `CHANGE_DIR`, `DEL_FILE` (Note: `LOAD_FILE` and `SAVE_FILE` are already implemented, but other directory commands are needed)
* **State Saving:** `SAVE_DOS_SCREEN`, `REST_DOS_SCREEN`

## 3. Summary
The language standard itself (variables, control flow, mathematical and boolean logic) is already fully implemented in MRMAC.

The strategic focus should be on Phase 1 and Phase 2. By implementing the core Text, Editor State, String, and Input operations, the majority of standard MEMAC macros will become functional. Phase 3 (Turbo Vision UI) provides a great opportunity to modernize the user interface of these macros while maintaining language compatibility.
