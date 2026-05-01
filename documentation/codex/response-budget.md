# Codex Response Budget

Codex must keep routine responses compact.

## Purpose

This document controls output length and report shape. It does not change architecture, code style, build rules, or protected-area contracts.

## Default output format

Use this format unless the maintainer explicitly asks for a longer explanation:

```text
Decision:
Files:
Change:
Build:
Warnings:
Next:
```

## Length limits

- Normal responses: maximum 25 lines.
- Implementation reports: maximum 40 lines.
- REJECT / ABORTED reports may be longer, but must stay focused on the blocking reason.
- Architecture decisions may be longer only when explicitly requested.

## Do not include

- Do not restate the task.
- Do not repeat AGENTS.md rules.
- Do not list every unchanged constraint.
- Do not explain obvious non-actions.
- Do not include motivational or conversational filler.
- Do not provide speculative alternatives unless asked.

## Result formats

### PASS

```text
Decision: PASS
Files:
Reason:
Next:
```

### REJECT

```text
Decision: REJECT
Files:
Blocking reason:
Minimal acceptable next step:
```

### IMPLEMENTED

```text
Decision: IMPLEMENTED
Files:
Change:
Build:
Warnings:
Next:
```

### ABORTED

```text
Decision: ABORTED
Files:
Reason:
Protected architecture touched:
Next:
```

## Protected architecture reporting

When protected architecture is not touched, report only:

```text
Protected architecture: no
```

When protected architecture is touched, report:

```text
Protected architecture: yes
Affected contract:
Affected files/functions:
Reason:
Required maintainer decision:
```

Do not continue implementation when protected architecture is touched incidentally.

## Audit responses

For audits, use:

```text
Decision: PASS / REFACTORABLE / PROTECTED / KEEP / REJECT
File:
Top findings:
Safe local changes:
Changes to avoid:
Risk:
Recommended tranche:
```

Limit findings to the five most relevant items.

## Implementation reports

For implementation reports, use:

```text
Decision:
Files:
Removed/inlined:
Kept:
Semantics:
Build:
Warnings:
Next:
```

Do not include full prose unless the build failed, the change was aborted, or a contract conflict was found.
