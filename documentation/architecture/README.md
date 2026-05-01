# Architecture Contracts

This directory contains binding contracts for the MR codebase.
These architecture contracts are normative only when reached through root `AGENTS.md`.
Root `AGENTS.md` defines priority and workflow.
This README is an index, not an independent instruction root.

The contracts are not design essays. They define boundaries that must not be blurred by incidental refactoring.

## Contract index

- `app-ui-dialogs-contract.md`
- `tvision-integration-contract.md`
- `settings-runtime-contract.md`
- `settings-bootstrap-contract.md`
- `settings-persistence-contract.md`
- `mrmac-language-contract.md`
- `vm-tvcall-contract.md`
- `keymap-contract.md`
- `coprocessor-deferred-ui-contract.md`
- `file-path-utilities-contract.md`
- `build-regression-contract.md`

## Global invariant

Do not collapse distinct roles into generic helpers:

- staging,
- validation,
- canonicalization,
- final apply,
- persistence,
- rendering,
- runtime execution.

When these roles interact, the contract for the relevant area must be read before changing code.
