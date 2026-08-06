# File / Path Utilities Contract

## Scope

Applies to:

- `app/utils/MRStringUtils.*`
- `app/utils/MRFileIOUtils.*`
- `app/MRPrivilegedFileBroker.*`
- path operations under `config/settings/`
- `ui/MRWindowSupport.cpp`
- `app/commands/MRExternalCommand.cpp`
- local path helpers in dialogs and commands.

## Authority

These utilities do not own domain state.
However, some setters using them may update history, dirty state or settings.

`MRPrivilegedFileBroker` is the sole elevated file-I/O boundary for a sudo
launch. The parent process owns only pre-authorized file descriptors and save
transactions. The editor application, settings bootstrap, VM, macros and
external commands run permanently as the invoking user.

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
- Elevated access is limited to existing regular files named explicitly as
  command-line file operands. Recursive traversal, glob expansion, macro paths,
  settings paths and later UI-selected paths are not elevated.
- Authorization is anchored by an opened directory descriptor, basename and
  source descriptor before privileges are dropped. Later requests must match
  the canonical authorized path and must not reopen an arbitrary client path.
- A privileged save writes a new adjacent file, verifies that the original
  fingerprint did not change, preserves owner and mode, copies supported
  extended attributes and commits by rename. Failure or client disconnect
  removes an uncommitted temporary file.
- Broker descriptors are close-on-exec. No subprocess may inherit the elevated
  channel.
- The editor process is lifetime-bound to the broker supervisor; losing the
  privileged parent must not leave an orphaned interactive editor.

## Boundaries

Without explicit maintainer approval:

- Changing path history semantics.
- Changing `~` expansion behavior.
- Changing fallback behavior for empty or relative paths.
- Creating a new universal path helper from non-identical local helpers.
- Treating string trim and path normalization as the same operation.
- Expanding the privileged allowlist after the application process starts.
- Allowing privileged creation of a path that did not identify an existing
  regular command-line file at launch.

## Related contracts

- [App / UI / Dialogs](app-ui-dialogs-contract.md)
- [Settings Runtime](settings-runtime-contract.md)

## Required manual tests

For path utility changes, test:

- empty path,
- relative path,
- absolute path,
- trailing slash,
- filename only,
- non-existing path,
- permission failure where relevant,
- sudo launch with user settings and an unreadable regular file,
- privileged save, backup, metadata preservation and concurrent-change rejection,
- editor termination during an uncommitted save,
- dialog path history if touched.
