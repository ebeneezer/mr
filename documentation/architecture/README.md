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
- `syntax-analysis-contract.md`
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

## Foundation reuse invariant

MR extends existing foundation through specialization, composition, configuration and dispatch.
MR does not duplicate existing contracts in parallel special-case paths.

New infrastructure is allowed only when no existing contract can carry the required behavior.
New types and code paths must own a distinct contract; they must not rebuild behavior already
provided by an existing layer or by a controlled specialization of that layer.
