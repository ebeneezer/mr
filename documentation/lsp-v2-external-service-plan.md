# LSP V2 External Service Plan

## Status

This document is a planning note, not an architecture contract.

It records the current direction for a second LSP attempt and the broader
external-service foundation that should precede it.

## Current Implementation Status

The V2 foundation is now usable by MR UI layers.

Implemented service foundation:

- long-lived external service process lifecycle for LSP,
- `Content-Length` framed JSON-RPC transport,
- request id tracking and response routing,
- deterministic LSP peer probes for transport and service behavior,
- editor-owned document mirrors with `didOpen` / `didChange`,
- debounced current-document sync after editor edits,
- stale-result rejection where server versions are available,
- URI/path mapping for current-file service results,
- MR workspace main-file marker projection as the service-context anchor,
- transient C/C++ compile context derived from MR compiler profiles and
  environment supplements,
- clangd fallback flags passed through LSP initialize options without project
  configuration files,
- coalesced `mr.log` trace of the effective clangd compile context.

Implemented LSP channels:

- diagnostics,
- definition,
- references,
- completion,
- hover,
- code actions,
- document symbols,
- workspace symbols,
- signature help,
- rename.

Implemented UI projection:

- MR-native editor context mini menu,
- LSP actions removed from the generic text menu,
- menu actions use the mouse position for right-click context,
- edit submenu with cut/copy/paste capability projection,
- read-only sidekick hover display with dwell-based mouse hover,
- diagnostic sidekick display when hovering diagnostic marker ranges,
- signature help sidekick display from the editor context menu,
- workspace-wide rename apply workflow over loaded MR workspace files,
- LSP result dialog for navigable result sets,
- diagnostics line in the top status area,
- diagnostic information markers in the editor text plane,
- diagnostic locations in the minimap.

Implemented color surface:

- `Context menu` and `Context menu selector` in code colors,
- `diagnostic information` in window colors,
- `diagnostics` in minimap colors,
- ANSI 16-color background selection in the color setup dialog.

Important behavior decisions:

- Context menu construction stays local and must not block on live LSP requests.
- Hover is informational and must not populate persistent LSP result lists.
- Diagnostics from clangd are shown at the locations reported by clangd.
- MR expands diagnostic hit ranges for mouse ergonomics without moving the
  displayed diagnostic location.
- End-of-line diagnostic locations are projected to the nearest visible editor
  cell so they can be seen and hovered.
- Diagnostic markers are remapped across local edits until the next LSP
  diagnostic publication replaces them.
- Workspace-wide rename is applied through MR-owned loaded workspace files.
  MR remains the document authority; LSP supplies the workspace edit.
- Multi-file search/replace can be restricted to the loaded MR workspace so the
  existing MFS engine can operate on the same user-facing workspace set.

Known real-server boundaries:

- `clangd` can omit diagnostic versions; MR must then use URI/current-document
  acceptance instead of version-based rejection.
- `clangd` may report follow-up parser errors at syntactically surprising
  locations. MR should display the reported LSP locations rather than guessing a
  "better" semantic location.
- GNU C nested functions remain a clangd semantic boundary, not an MR mirror
  failure.

Open LSP channels not yet projected into MR UI:

- document highlight,
- formatting and range formatting,
- semantic tokens,
- inlay hints,
- call hierarchy and type hierarchy,
- folding ranges.

Recommended next usable tranche:

1. Bento LSP integration.
2. Document highlight, using transient editor markers rather than persistent
   diagnostics, after Bento has a stable pane-aware LSP projection model.
3. Formatting, only after the edit-application path is reviewed separately.

Semantic tokens should remain a later tranche because they interact with the
protected syntax and color architecture.

## Next Tranche: Bento LSP Integration

The current LSP UI projection was built and validated primarily for single
editor windows. Before adding more LSP channels, Bento must become a first-class
LSP projection surface.

The Bento tranche must make LSP targeting pane-aware:

- right-click context actions must target the editor pane under the mouse,
- hover must target the editor pane under the mouse,
- keyboard-triggered LSP actions must target the focused editor pane,
- LSP result jumps must restore the correct MR window and pane context,
- status and diagnostics text must remain globally visible but identify the
  affected document clearly.

SideKick projection must respect Bento geometry:

- SideKick placement must be computed relative to the owning editor pane, not
  only the desktop or top-level edit window,
- SideKicks must avoid covering the code location when usable space exists
  above, below or beside the location,
- neighboring Bento panes and Bento chrome must not be treated as expendable
  drawing space,
- the existing single-editor SideKick behavior must remain unchanged.

Diagnostics and markers must remain editor-owned:

- diagnostic text markers stay attached to the editor buffer that owns the
  diagnostic,
- minimap diagnostics remain pane-local,
- diagnostic hover hit ranges stay ergonomic but must not cross pane
  boundaries,
- stale diagnostic replacement remains driven by LSP publication, not by Bento
  redraw.

The tranche is complete when the implemented LSP channels behave consistently
in at least a two-pane Bento setup:

- context menu,
- hover,
- signature help,
- diagnostics markers and diagnostic hover,
- definition/references/result jumps,
- workspace symbols and rename where the selected target is already loaded in a
  Bento pane.

## Completion Decision

The usable LSP V2 editor/UI tranche is complete.

The remaining items are not foundation blockers and not required for UI
integration of the implemented LSP service layer. They are optional later
feature tranches and should be planned separately because they touch different
editor surfaces:

- document highlight touches transient marker projection,
- formatting touches edit-application policy,
- semantic tokens touch protected syntax/color architecture,
- inlay hints touch inline layout,
- hierarchy channels need a separate navigation/result UI decision,
- folding ranges overlap MR's existing folding model.

Document highlight is deliberately deferred until Bento targeting and SideKick
geometry are pane-aware. Otherwise it would be implemented against the
single-editor projection model and then need to be corrected for Bento.

## Retrospective

The first LSP attempt failed because it treated LSP primarily as an editor
feature. The missing foundation was a controlled external-service layer.

LSP diagnostics, navigation, hover and completion are visible editor features,
but the hard problem is below that surface:

- external process lifecycle,
- bidirectional stream handling,
- protocol framing,
- JSON-RPC request and notification routing,
- document versioning,
- stale response rejection,
- URI and path mapping,
- asynchronous result application,
- service capability projection into MR-native UI.

The first implementation strategy reached too quickly into UI-visible behavior.
For V2 the foundation must be built first.

## Core Direction

MR should gain a controlled external-service layer.

The layer must support LSP, but it must not be LSP-specific in its lowest
parts. It should later also carry Git, build tools and possibly debugger
protocols.

Conceptual stack:

```text
MR Workspace
  Benutzerwahrheit

Service Contexts
  abgeleitete technische Wahrheiten pro Dienst

External Process Foundation
  wiederverwendbare Prozess-/Stream-/Lifecycle-Schicht

Protocol Adapters
  LSP JSON-RPC
  Git CLI
  Build CLI
  spaeter DAP

Feature Consumers
  diagnostics
  definition
  references
  completion
  hover
  Git status/diff
  build output
```

The architectural question is not which feature is displayed first. The
architectural question is which layer owns which truth.

## Authority Model

MR owns the editor truth.

Authoritative MR state includes:

- open documents,
- text buffers,
- document identity,
- document dirty state,
- document version,
- cursor and selection state,
- workspace membership,
- main file selection.

External services may receive mirrors or derived service contexts. They must not
become authoritative for MR editor state.

For LSP this means:

- MR keeps the canonical document text and version.
- The LSP layer keeps a `LspDocumentMirror` representing the last state sent to
  the server.
- Incoming results are accepted only if they still match the relevant MR
  document version or an explicitly valid service version.
- Old diagnostics, completion responses or navigation results must be discarded
  when they belong to stale document state.
- Server state must never repair, replace or override MR text state.

This is a protocol mirror, not shared ownership.

## LSP Object Boundaries

An all-owning `LSPManager` must be avoided.

Useful OOP boundaries are narrower:

```text
MRDocument
  authoritative editor text and version

LspDocumentMirror
  last document state sent to the language server

LspSession
  server lifecycle, initialize/shutdown, request ids, notifications, responses

LspServiceAdapters
  diagnostics, definition, references, completion and hover as separate consumers

MR UI Consumers
  context menu, hover projection, diagnostics pane, sidekick, command routing
```

The LSP layer may own protocol state. It must not own MR document state,
workspace truth or UI behavior.

## External Process Foundation

The reusable foundation should be below service semantics.

Generic:

- process start and stop,
- working directory and environment,
- stdin/stdout/stderr stream ownership,
- exit status,
- asynchronous read/write events,
- bounded shutdown behavior,
- error reporting suitable for higher layers.

Protocol-specific:

- LSP `Content-Length` framing,
- JSON-RPC request id tracking,
- JSON-RPC notifications,
- Git CLI output parsing,
- build output parsing,
- later debugger protocol behavior.

The foundation should not become a generic semantic "external tool manager".
Process and stream mechanics can be shared. Git, LSP and build semantics should
remain separate service adapters.

## Deterministic Test Server

V2 needs a deterministic protocol peer.

This can be called a fake LSP, test LSP or deterministic LSP test server. It is
not a real language server. Its purpose is to test MR's communication layer.

Required behavior:

- parse and emit `Content-Length` framed JSON-RPC,
- answer `initialize`,
- accept `initialized`,
- accept `textDocument/didOpen`,
- accept `textDocument/didChange`,
- emit deterministic `textDocument/publishDiagnostics`,
- answer selected request types with known payloads,
- deliberately delay selected responses,
- emit malformed or unexpected messages for error-path tests.

Real servers such as `clangd`, Python LSP or TypeScript LSP are integration
targets after the communication layer is proven. They are not suitable as the
primary regression opponent.

## MR Workspace Model

MR should not become a folder-first IDE.

The MR workspace is the user-facing project model:

- all loaded files may belong to the MR workspace,
- one loaded file can be marked as the main file,
- workspace membership is MR-owned,
- service roots are derived from the MR workspace, not the other way around.

Design rule:

```text
MR Workspace owns membership.
Main file owns semantic anchor.
Filesystem roots are derived service contexts, never authoritative workspace membership.
```

The main file is not just decoration. It is the anchor for project semantics:

- build command selection,
- compile context,
- include context,
- LSP root derivation,
- header interpretation,
- future debugger/run context.

The main file should be visible in the icon area. That icon is a projection of
workspace state; it must not become separate UI-owned state.

## Workspace Adapter

The workspace adapter answers service-context questions without turning the
filesystem into MR's authority.

It should derive:

- workspace files from loaded MR files,
- main file from explicit MR workspace state,
- effective root for protocols that require a root,
- build working directory,
- compile context,
- LSP `rootUri`,
- LSP `workspaceFolders` when needed,
- per-file Git repository context.

For LSP, the effective root is adapter data. It is not the user-facing project
definition.

For build tools, the main file and build profile are the semantic anchor.

For Git, the `.git` repository remains authoritative for Git operations.

## LSP Compile Context

MR-native setup is the authority for C/C++ compile context.

The LSP adapter must not silently discover or consume project-local JSON or
clangd configuration files. In particular, MR V2 does not use automatic
`compile_commands.json`, `.clangd` or `compile_flags.txt` discovery.

Effective C/C++ LSP context is derived from:

1. the active MR compiler profile,
2. the MR workspace main file as semantic root/working-directory anchor,
3. the process environment as a non-persistent runtime supplement.

The environment may contribute include paths and flags, for example:

- `CPATH`,
- `C_INCLUDE_PATH`,
- `CPLUS_INCLUDE_PATH`,
- `CPPFLAGS`,
- `CFLAGS`,
- `CXXFLAGS`,
- `CC`,
- `CXX`.

MR compiler profile data wins over environment-derived data. Environment data is
not written back to settings and does not become MR configuration.

For clangd, MR passes the effective context as `initializationOptions`
`fallbackFlags` and starts clangd with `--compile_args_from=lsp` and
`--strong-workspace-mode`. This keeps the context transient and avoids
project-local `compile_commands.json`, `.clangd` or `compile_flags.txt` files.

The adapter logs the effective compile context as one coalesced `mr.log` entry
when starting or replacing a clangd runtime. The log states the origin of
derived values, for example MR compiler profile, workspace main file or
environment variable. This is diagnostic traceability, not user-facing message
line traffic.

If a concrete language server later requires information not modeled by MR, MR
must make an explicit design decision: add a MR-native setup field, extend the
server adapter, or reject that service for V2. It must not add hidden folder
configuration discovery as a workaround.

## Git Context

Git should remain Git.

MR should not reimplement Git semantics and should not pretend that a Git
repository is something other than the actual repository on disk.

Likely command style:

```sh
git -C <repo-root> status
git -C <repo-root> diff
git -C <repo-root> log
```

Repository root detection should be Git-native, for example through `.git`
search or `git -C <path> rev-parse --show-toplevel`.

If an MR workspace contains files from multiple repositories:

- file-local commands use the repository of the current file,
- mainfile-local commands use the repository of the main file,
- workspace-wide commands group output by repository,
- files outside any repository are reported as not versioned,
- commands requiring a single repository must ask for an explicit repository
  choice or report that the workspace spans multiple repositories.

Git repository membership must not redefine MR workspace membership.

## No Visible JSON Control Files

MR must not require visible editor JSON files in the project.

V2 should avoid:

- `.vscode`-style project control,
- mandatory `settings.json`,
- mandatory generated `compile_commands.json` in the project,
- automatic discovery of `compile_commands.json`, `.clangd` or
  `compile_flags.txt`,
- passive behavior changes triggered by hidden interpretation of JSON or
  project-local language-server configuration files.

MR V2 does not use external project configuration files as an automatic
configuration source. If such support is ever desired, it must be designed as an
explicit MR-native import or adapter decision, not as folder magic.

If a language server requires configuration data:

1. Prefer MR setup, direct server arguments or LSP initialization options.
2. If a file is unavoidable, generate it in a private MR temp/cache location.
3. Remove or ignore volatile generated files after the session as appropriate.
4. If a server cannot operate without visible project control files, it is not a
   good V2 target.

MR-native configuration belongs in MR-controlled state. Persistent integration
with settings or workspace serialization requires a separate protected
architecture tranche.

## Context Menu

The context menu should be a central higher-layer design idea.

It must be MR-native and deterministic. Opening a context menu must not block on
live external service requests.

Design rule:

```text
MR offers available capabilities at the current location.
MR does not ask external services while constructing the context menu.
```

Menu construction should use:

- local editor context,
- current file state,
- current cursor or mouse position,
- registered service capabilities,
- cached service results,
- known active debug/build/LSP/Git contexts.

Examples of capability offers:

- go to definition,
- find references,
- show diagnostics,
- show hover details,
- show variable value when a runtime/debug context exists,
- Git blame for a versioned file,
- Git diff for a versioned file,
- build diagnostics for the main file context.

The action selected from the menu may use the appropriate service. The menu
itself should not be a waiting room for LSP, Git or build tools.

## Hover

Hover should be an information channel, not an authority channel.

Staged dwell times are a valid MR interaction idea:

```text
short dwell:
  local hints, diagnostic summary, status-level information

medium dwell:
  symbol hover, type information, Git blame summary

long dwell:
  documentation, signature details, extended analyzer/build explanation
```

Hover must not block editing. It should use the same local context and result
cache model as the context menu, so mouse and keyboard paths do not become
separate systems.

Context menu is the primary action channel. Hover is the staged information
channel.

## Service Result Model

External services should produce service results. UI layers consume those
results.

Examples:

- diagnostics,
- definition targets,
- reference lists,
- completion items,
- hover text,
- Git file status,
- Git hunks,
- build output diagnostics,
- debugger variable values.

Service results must carry enough metadata to reject stale or mismatched data:

- document identity,
- document version or service version,
- source service,
- request id where relevant,
- validity state,
- error state.

UI consumers should not need to know protocol mechanics.

## First LSP V2 Tranche

The first implementation tranche should be intentionally small.

Recommended scope:

- external process foundation sufficient for one long-lived service,
- LSP content-length JSON-RPC codec,
- `initialize` / `initialized` / `shutdown` / `exit`,
- one document mirror,
- full document sync only,
- deterministic test LSP peer,
- diagnostics-only service result,
- no completion,
- no definition,
- no references,
- no persistent settings,
- no visible project JSON,
- no broad UI integration.

The first visible UI can be minimal diagnostic projection after the transport
and stale-result rules are proven.

## Later Tranches

Possible later tranches:

1. Workspace adapter with mainfile projection in the icon area.
2. LSP diagnostics projection into existing diagnostic surfaces.
3. Context-menu capability registry.
4. Definition and references.
5. Completion.
6. Hover with staged dwell behavior.
7. Git service context using real `.git` repositories.
8. Build service context anchored at the main file.
9. Persistent workspace/LSP profiles through an explicit protected architecture
   plan.
10. Debugger/runtime variable value service, likely separate from LSP.

Runtime variable values are not an LSP feature. They require a build and debug
context, likely through a debugger protocol or MR-specific runtime integration.

## Validated Manual LSP Test Path

The first real-server validation path uses `clangd` with an unsaved C buffer.

The important property is that MR must send the current editor buffer mirror to
the language server. The test file on disk may be stale. Successful results
therefore prove that MR's `didOpen` / `didChange` path, document versioning and
service result projection are using the editor state, not only the filesystem
state.

Recommended unsaved C source:

```c
#include <stdio.h>

int helper(void) {
	return 1;
}

int main(void) {
	int unused;

	puts("Hello world");
	puts("Hello world");

	helper();
	helper();

	return 0;
}
```

Manual checks:

- Put the cursor on a `helper` call and run LSP definition.
- Expected: MR jumps to the file-scope `helper` definition in the unsaved
  editor state.
- Put the cursor on `helper` and run LSP references.
- Expected: MR shows the definition plus both local calls.
- Put the cursor on `helper` and run LSP hover.
- Expected: hover shows the local `helper` function signature.
- Type a `he` / `hel` prefix in the same unsaved buffer and run LSP completion.
- Expected: `helper()` appears as a completion item and insertion inserts only
  the symbol text chosen by the server.

Known clangd boundary:

```c
int main(void) {
	int helper(void) {
		return 1;
	}

	helper();
}
```

GCC accepts nested functions as a GNU C extension. `clangd` does not model this
as valid C for definition / references. This is not an MR mirror failure. It is
a language-server semantic boundary.

Observed real-server response shapes that MR must tolerate:

- `textDocument/definition` may return `null`, a single `Location`,
  `Location[]` or `LocationLink[]`.
- `textDocument/references` returns `Location[]`; `range` fields may list
  `end` before `start`.
- `textDocument/publishDiagnostics` may omit `version`.
- Missing diagnostic version means MR cannot perform version-based stale
  rejection and must fall back to URI/current-document acceptance.

## Constraints To Preserve

- MR workspace membership is user-facing truth.
- Git repository roots are Git truth only.
- LSP roots are protocol adapter data.
- Main file is the semantic anchor for build and project context.
- External services do not own MR document state.
- Context menus are constructed locally from available capabilities.
- Hover is informational and staged.
- Visible project JSON is not required and is not silently discovered.
- C/C++ compile context comes from MR compiler profiles plus non-persistent
  environment supplements.
- Persistent settings and workspace serialization changes require separate
  protected planning.
