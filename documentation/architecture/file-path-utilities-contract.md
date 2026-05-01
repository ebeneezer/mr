# File / Path Utilities Contract

## Scope

Applies to:

- `app/utils/MRStringUtils.cpp`
- `app/utils/MRFileIOUtils.cpp`
- `config/MRDialogPaths.cpp`
- `ui/MRWindowSupport.cpp`
- `app/commands/MRExternalCommand.cpp`
- local path helpers in dialogs and commands.

## Authority

These utilities do not own domain state.
However, some setters using them may update history, dirty state or settings.

## Invariants

- Do not assume similarly named path helpers are semantically identical.
- Compare behavior before replacing:
  - empty string,
  - relative path,
  - absolute path,
  - trailing slash,
  - backslash,
  - `~`,
  - file without directory,
  - root path,
  - error behavior.
- Do not introduce new local duplicates for path expansion, trimming or file checks.
- Do not centralize a helper unless semantics are proven identical or deliberately changed.

## Allowed

- Reuse existing utilities when semantics are proven identical.
- Remove dead helpers.
- Replace duplicate file-existence checks with standard library calls where behavior matches.

## Forbidden without explicit approval

- Changing path history semantics.
- Changing `~` expansion behavior.
- Changing fallback behavior for empty or relative paths.
- Creating a new universal path helper from non-identical local helpers.
- Treating string trim and path normalization as the same operation.

## Required tests

For path utility changes, test:

- empty path,
- relative path,
- absolute path,
- trailing slash,
- filename only,
- non-existing path,
- permission failure where relevant,
- dialog path history if touched.
