# TVision Upstream Drafts

Stand: 2026-03-29

Repository:

- <https://github.com/magiblot/tvision>

## Draft 1: Issue

### Titel

`TCommandSet accepts out-of-range command ids and can corrupt memory`

### Text

While integrating Turbo Vision into a larger application on Linux, I hit a memory corruption issue caused by out-of-range command IDs passed to `TView::enableCommand`.

`TCommandSet` currently stores command state in a fixed `uchar cmds[32]`, but `has`, `enableCmd` and `disableCmd` do not validate the incoming command number before indexing the array.

As a result, values above the supported range can write or read past the end of the command bitset:

- `include/tvision/views.h`: `uchar cmds[32]`
- `source/tvision/tcmdset.cpp`: `cmds[loc(cmd)] |= mask(cmd);`

With AddressSanitizer this shows up as a buffer overflow in `TCommandSet::enableCmd(int)`.

I realize passing such a command id is an application error, but the current behavior turns it into silent memory corruption. A defensive range check would make the library much more robust and easier to debug.

I prepared a minimal patch that makes:

- `has(cmd)` return `False` for out-of-range values
- `enableCmd(cmd)` ignore out-of-range values
- `disableCmd(cmd)` ignore out-of-range values

This does not change the public API and keeps behavior deterministic.

## Draft 2: PR

### Titel

`Guard TCommandSet against out-of-range command ids`

### Text

This patch adds a small defensive range check to `TCommandSet`.

Problem:

- command state is stored in a fixed 256-bit array (`uchar cmds[32]`)
- `has`, `enableCmd` and `disableCmd` index into it without validating the command id
- out-of-range values can therefore trigger out-of-bounds reads/writes

Why this change:

- avoids memory corruption from invalid command ids
- improves diagnostics when integrating TVision into larger applications
- keeps the API unchanged
- keeps valid command behavior unchanged

Behavior after this patch:

- `has(cmd)` returns `False` if `cmd` is outside the supported range
- `enableCmd(cmd)` becomes a no-op if `cmd` is outside the supported range
- `disableCmd(cmd)` becomes a no-op if `cmd` is outside the supported range

I kept this patch intentionally narrow so it can be reviewed independently of any application-specific code.

## Draft 3: Tiny follow-up PR

### Titel

`Add missing tv.h include to internal/codepage.h`

### Text

`include/tvision/internal/codepage.h` uses `TStringView` but does not include `<tvision/tv.h>` directly.

Adding the include makes the dependency explicit and avoids build breakage when the header is included in isolation or through a different include path than expected.

This is a tiny header hygiene fix only.
