# Syntax Analysis Contract

## Purpose

This contract defines how MR uses MR-owned syntax analysis and derived syntax data for syntax coloring, smart indentation and code folding.

MR-owned syntax data is derived editor data and must not be treated as the canonical syntax source.

## Source authority

The source text, document id, document version and configured language are the authority for syntax-derived state.

Derived syntax caches must be rebuilt or invalidated from those inputs. They must not become independent source truth.

## Derived editor data

Derived syntax data is MR-owned editor cache data.

Derived data may include:

- color runs,
- fold ranges,
- indent hints,
- structural line records,
- symbol or outline candidates.

Derived data may be rebuilt, discarded or replaced when the document version, language or memory budget changes.

Derived data must not become the canonical syntax source.

## Parallelism

Parallel workers must emit MR-owned derived data only.

The merge boundary for parallel syntax work is an MR data structure boundary, such as color runs, fold ranges or indent hints.

## Large-file policy

Large files use budgeted analysis.

Budgeted analysis means prioritized and staged analysis, not knowingly wrong syntax semantics.

The visible viewport and cursor-near area have priority.

MR may delay, discard or avoid far-away derived syntax caches under memory pressure.

MR must prefer missing or conservative semantic assistance over wrong semantic assistance.

## Coloring

Syntax coloring may use staged quality levels.

Coloring may be absent, lexical, stale or structural, provided the state is represented explicitly.

Large files should prefer compact color runs over per-character token maps.

A stale or provisional coloring state must not be hidden from the syntax cache model.

## Smart indentation

Smart indentation must use reliable MR-derived structural context when available.

If reliable structural context is unavailable, indentation must fall back conservatively.

A conservative fallback may preserve the previous non-empty line indentation or handle obvious bracket cases.

The fallback must not pretend to be structurally exact.

## Folding

Fold ranges must only be offered when the range is reliable.

If a fold range is not structurally reliable, MR must not show the fold marker.

A missing fold is preferable to a wrong fold.

## Provisional analysis

Provisional chunk or halo analysis may be used for early rendering only.

Provisional analysis must not become the canonical structural source.

Provisional analysis must not drive smart indentation or folding unless the affected range is explicitly validated as reliable.

## Implementation rule

Do not introduce generic syntax helper abstractions that blur these roles:

- canonical parse,
- derived editor data,
- rendering cache,
- indentation decision,
- folding range,
- large-file budget policy.

When syntax analysis changes touch the coprocessor or editor rendering, the plan must state which role is being changed and which roles are deliberately left unchanged.
