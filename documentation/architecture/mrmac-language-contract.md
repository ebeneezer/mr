# mrmac Language / Compiler Contract

## Scope

Applies to:

- `mrmac/lexer.l`
- `mrmac/parser.y`
- `mrmac/mrmac.c`
- `mrmac/MRMacroRunner.cpp`
- bytecode generation paths.

## Authority

The mrmac language, parser and compiler define executable macro semantics.

Settings use `MRSETUP(...)` as serialized macro source in controlled startup mode, but language semantics are broader than settings.

## Invariants

- Do not change grammar as incidental cleanup.
- Do not change opcodes without a dedicated migration decision.
- Do not change macro compile behavior during UI or settings refactoring.
- Macro names are not limited to eight characters.
- Generated parser/lexer artifacts are build-critical.

## Allowed

- Local bug fixes with regression proof.
- Parser diagnostics improvements when text changes are approved.
- Compiler fixes that preserve existing documented semantics.

## Forbidden without explicit approval

- Grammar rewrites.
- Opcode renumbering or semantic changes.
- Treating settings cleanup as macro-language cleanup.
- Introducing hidden compatibility rewrites.
- Changing generated artifact policy incidentally.

## Required tests

For language/compiler changes, test:

- clean build,
- parser generation,
- representative macro compile,
- VM execution of compiled bytecode,
- relevant regression checks.
