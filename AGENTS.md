# AGENTS.md

## Purpose

This repository is not developed by vibe coding.

The agent is a technical implementation assistant, not an autonomous architect. Changes must remain locally readable, reviewable, reproducible, and aligned with the maintainer's style.

The project uses C++20 as the compiler standard. This does not mean that every modern C++ idiom is welcome. The preferred style is controlled, explicit, machine-oriented C++.

The primary optimization target is maintainability by a C/C++ systems programmer reading the code locally.

## Communication

- Write plans, reviews, PR text, commit messages, and explanations in German.
- Keep code, identifiers, existing upstream text, and technical literals in English.
- Address the maintainer formally as "Sie" or "Dr. Raus".
- State technical concerns directly.
- Do not use reassuring filler text.
- If a trade-off exists, give a reasoned recommendation.

## Project identity

MR is not merely an editor with a macro language.

Core project areas are:

- `mrmac`,
- lexer and parser,
- bytecode compiler,
- stack machine / VM,
- `settings.mrmac` serialization,
- TVision-based TUI integration.

The editor is the visible application layer around this macro processor architecture.

## Controlled C++20 subset

Use a controlled C++20 subset.

Preferred:

- explicit control flow,
- concrete types,
- clear ownership,
- local readability,
- low indirection,
- domain-specific naming,
- direct debuggability,
- classic C++ structure with selective use of the standard library.

C++20 features are allowed only when they make the code clearer, safer, or simpler in this codebase.

A shorter implementation is not better if it is less explicit.

Modern C++ idioms are not automatically improvements.

## Preferred constructs

Use these freely when appropriate:

- normal classes, structs, enums, and functions,
- explicit loops,
- `std::string`,
- `std::vector`,
- `std::map`,
- `std::unordered_map`,
- `std::optional`,
- RAII for real resource management,
- simple inheritance when it follows the existing TVision model,
- `constexpr` for real constants,
- `std::filesystem` instead of custom file-existence wrappers,
- existing project utilities instead of local reinvention.

Use `auto` only when the type is obvious or excessively redundant, such as iterators or factory-return expressions where the concrete type is not useful to the reader.

Do not use `auto` when the exact type carries meaning for ownership, lifetime, container semantics, or API behavior.

## Forbidden by default

Do not introduce these unless explicitly justified in the implementation plan and approved before coding:

- new templates,
- Concepts,
- generic lambdas,
- nested lambdas,
- type erasure,
- visitor machinery,
- `std::variant` dispatch for simple control flow,
- fluent STL algorithm chains where a loop is clearer,
- framework-style abstractions,
- new utility files,
- new manager/helper/handler classes without domain meaning,
- wrapper types that add type mechanics without a real domain concept.

If a simple loop is clearer, use the loop.

## Abstraction budget

The abstraction budget is zero by default.

A new abstraction is allowed only if it clearly does at least one of the following:

- removes real duplication from at least two call sites,
- names a stable domain concept,
- isolates a clear boundary to TVision, mrmac, VM, platform code, or serialization,
- makes a long existing operation materially easier to understand.

Moving complexity into more files is not complexity reduction.

Do not optimize for generic elegance.

## Default rule: no new helpers

By default, do not introduce new helper functions.

A helper requires prior justification and must be listed in the plan before implementation.

A helper is rejected if it:

- wraps only one or two local statements,
- hides simple control flow,
- exists only to make code look cleaner,
- forces the reader to jump between multiple tiny functions,
- duplicates an existing project function,
- is named after mechanics instead of a domain concept,
- calls other tiny helpers in a cross-calling chain.

When in doubt, keep the code inline and explicit.

## Reuse before creation

Before adding any function, class, struct, enum, utility, or file, search the existing codebase for equivalent or related functionality.

The implementation plan must name the existing functions/classes considered.

If no existing function is reused, the plan must explicitly say why.

Do not create a new local variant of existing behavior.

Known examples of behavior that must not be duplicated casually:

- path expansion,
- path normalization,
- path trimming,
- file-existence checks,
- settings access,
- settings serialization,
- dialog validation,
- history persistence.

## Local readability

Code should be readable from top to bottom in one file whenever reasonable.

Local readability has priority over:

- generic elegance,
- maximal reuse,
- abstract DRY purity,
- idiomatic modern C++,
- fewer lines of code.

DRY is not mechanical. Two simple readable sites may be better than one artificial abstraction.

Do not scatter a small behavior across multiple tiny functions unless there is a clear domain boundary.

## Naming

Identifiers must describe what something is or does in domain terms.

Avoid:

- leading underscores,
- trailing underscores,
- vague names such as `Helper`, `Util`, `Manager`, `Handler`,
- suffixes like `Choice` when the object is not actually a choice,
- names that describe implementation mechanics when a domain name is possible.

Longer names are acceptable when they are more precise.

Macro names are not limited to eight characters.

## Control flow

Clear control flow is more important than cleverness.

- Simple `if` statements are allowed.
- A one-line `if` is acceptable when it improves readability.
- Use `switch` when it makes a real decision structure clearer.
- Use table-driven code only when it clarifies a real table-like domain.
- Do not introduce table-driven code merely to avoid visible `if` statements.
- Do not replace readable control flow with STL chains for style reasons.

## Headers and source files

- Headers normally contain declarations.
- Implementations belong in `.cpp` files.
- Inline code in headers requires a technical reason.
- New source files or directories require prior approval.
- Do not modify sources under `tvision`.
- Ignore `misc` as a scratch/test area unless explicitly asked.
- Do not treat files under `documentation` as source files.

## TVision rules

TVision upstream proximity is mandatory.

- Do not work around TVision when a standard TVision-like solution exists.
- Do not modify TVision upstream code.
- Do not introduce direct screen-writing paths.
- Do not manipulate `TScreen::screenBuffer` for new functionality.
- Use TVision concepts instead of bypassing them.
- New UI functionality must go through `TView`, `TDrawBuffer`, dialogs, commands, or other TVision-near mechanisms.

No overlay hacks.

No event-interception hacks when a proper TVision mechanism exists.

## mrmac, VM, and UI boundaries

The mrmac VM, TVCALL, and macro screen commands are producers.

They may produce staged:

- `MRMacroDeferredUiCommand` mutations,
- `MacroCellGrid` mutations,
- domain commands for the UI layer.

The UI layer is the consumer:

- `MacroCellGrid`,
- `MacroCellView`,
- screen facade,
- TVision projection on the UI thread.

New TVCALL screen operations must be modeled as commands in this layer.

Goals:

- no competing write paths,
- no ghosting,
- no hidden UI side effects,
- clear producer/consumer separation.

## Architectural red lines

The following areas are architectural boundaries and must not be changed opportunistically:

- settings runtime model,
- `settings.mrmac` serialization,
- `MRSETUP` semantics,
- mrmac VM execution,
- TVCALL screen command flow,
- `MacroCellGrid` / `MacroCellView` producer-consumer model,
- TVision drawing and dialog mechanics,
- file/path history model,
- backup/autosave persistence.

Changes in these areas require an explicit architecture note before implementation.

No incidental cleanup is allowed in these areas.

## Settings model

There is exactly one authoritative runtime model for settings: the central key/value store.

`settings.mrmac` is only the serialized representation of this state.

Rules:

- no decentralized settings registries,
- no shadow copies,
- no parallel stores,
- no reload workarounds,
- no save/reload cycles,
- no runtime reconstruction by merging old files,
- no semantic migration of obsolete keys unless explicitly approved.

After bootstrap, settings are changed only in the central in-memory model.

Applying active settings is done from that model through the mrmac VM.

Terms and code paths such as "reload settings" are conceptually wrong and should be avoided.

## MRSETUP rules

- Settings are serialized through `MRSETUP(...)`.
- Repeated `MRSETUP` statements are allowed for lists and record-like structures.
- Keys must be unambiguous.
- Do not create two spellings for the same value.
- Unknown or obsolete keys are discarded during bootstrap.
- Missing current keys are filled from current defaults.
- Legacy ballast is intentionally cut off.

Record-like structures such as key maps, histories, and future profile entries must be modeled explicitly.

Do not rely on accidental last-wins behavior unless the data is explicitly an ordered history.

## Dialog rules

Dialogs must remain TVision-near.

- Use two spaces from the frame to dialog elements.
- Labels are left-aligned, normal-colored, and end with a colon.
- Use one space between label and input field.
- Browse/magnifier buttons are placed at the end of fields.
- Radio-button clusters are aligned cleanly.
- Cluster labels are left-aligned above their cluster.
- Large dialogs use `MRScrollableDialog`.
- Dialogs switch to scrollview mode when the terminal is too small.
- Validators do not show error dialogs.
- Validators report warnings through the message line / marquee.
- A full validator pass runs when entering a dialog.
- The Done button is ghosted when the dialog state is invalid.
- Dialogs contain no static help prose.
- Every dialog needs a Help button.
- Dialogs use the colors of the Menu/Dialog group from color setup.

Dirty state must be set only by real changes.

Do not trigger redundant settings writes.

## History rule

Do not use TVision history in the project codebase.

Do not use:

- `THistory`,
- `historyAdd`,
- upstream history IDs for project dialog histories.

Path and file histories use project-owned:

- key/value serialization,
- runtime models,
- explicit ordering semantics.

## Bugfix rule

Bugfixes fix causes, not symptoms.

Forbidden:

- guard backpacks,
- watcher workarounds,
- parallel old/new mechanisms without an explicit migration decision,
- additional defensive layers when the root cause is still unexamined,
- save/reload cycles to paper over state problems.

If the cause is not known, state that explicitly.

Do not disguise uncertainty as architecture.

## Refactoring rule

Refactorings require explicit approval.

Before refactoring, explain:

1. the concrete problem being solved,
2. affected files,
3. existing functionality to be reused,
4. new functions/classes/types to be introduced,
5. why a smaller change is insufficient,
6. which abstractions are deliberately avoided.

Without approval, do not perform structural refactoring.

## Implementation protocol

Before code changes, provide a plan.

The plan must list:

1. affected files,
2. existing functions/classes that will be reused,
3. new functions/classes/types, if unavoidable,
4. why each new helper or type is necessary,
5. abstractions deliberately avoided,
6. impact on settings, UI, VM, serialization, and persistence,
7. compile/test plan.

If these points cannot be answered clearly, the change is not ready for implementation.

For small edits, the plan may be short, but it must still identify reuse and new helpers.

## Review rejection triggers

A proposed change should be rejected if it:

- introduces helpers without prior justification,
- splits a linear operation into cross-calling mini-functions,
- ignores existing project utilities,
- replaces readable loops with opaque STL chains,
- introduces modern C++ idioms without local readability gain,
- changes architectural boundary areas without an architecture note,
- adds indirection without reducing net complexity,
- changes naming away from semantic clarity,
- preserves obsolete mechanisms in parallel with new ones.

When rejected, redesign first. Do not patch the bad design incrementally.

## Quality assurance

A change to shared infrastructure is not complete merely because one example works.

For changes to:

- dialogs,
- settings,
- serialization,
- key/value keys,
- history,
- path logic,
- VM,
- TVCALL,
- resolvers,
- shared services,

identify the affected consumer set.

Check at least:

- creation path,
- reopen path,
- success path,
- failure path,
- persistence,
- restart/reload of application,
- existing data,
- conflict paths.

## Migration rule

An old mechanism is replaced only when:

- the old mechanism has been removed, or
- the remaining pieces are explicitly named and justified.

Hidden parallel operation is forbidden.

Silent partial migration is forbidden.

"Works in one place" is not completion.

## Build rule

Before handoff, run a real clean compile:

```sh
make clean all CXX=clang++
```

Report compile errors, warnings, or unverified build state explicitly.

Do not remove or break existing `paplay` success/failure signals in the Makefile.

## Handoff rule

Handoffs are delivered as a `tar.bz2` archive containing:

- changed files at correct relative paths,
- an apply script if useful,
- no placeholders such as `/path/to/...`,
- no patch chains.

The handoff must be directly usable by the maintainer.

## Final rule

Explicit, controlled, maintainable system programming is the project style.

Do not optimize for modern-looking C++.

Do not optimize for generic elegance.

Do not optimize for vibe-coded momentum.

Optimize for code the maintainer can still read, debug, and own.
