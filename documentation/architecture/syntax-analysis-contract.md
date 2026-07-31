# Syntax Analysis Contract

## Scope

Applies to:

- `ui/MRSyntax*` and `ui/syntax/`,
- `derivedstate/MRSyntaxDerivedState.*`,
- `derivedstate/MRFoldingDerivedState.*`,
- syntax, folding and indentation paths under `ui/MRFileEditor/`,
- outline producers consuming canonical fold data,
- coprocessor payloads and adoption for derived syntax state.

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

## Rendering boundary

Editor rendering is a consumer of derived syntax data.

Draw and other UI rendering paths must not perform syntax analysis synchronously.

Rendering may read existing derived caches, request or schedule analysis work, and render plain, stale or provisional coloring when cache data is missing.

Cache misses must not turn the UI thread into a syntax worker. Syntax analysis that creates or refreshes token runs, structural line state, fold ranges or indentation hints belongs to the coprocessor and pumped apply path.

This is the standard rule for all file sizes. Large files do not get a separate rendering architecture; they only make the latency risk visible sooner.

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
- rendering consumer,
- indentation decision,
- folding range,
- large-file budget policy.

When syntax analysis changes touch the coprocessor or editor rendering, the plan must state which role is being changed and which roles are deliberately left unchanged.

## Related contracts

- [Coprocessor Runtime](coprocessor-runtime-contract.md)
- [TVision Integration](tvision-integration-contract.md)

## Required manual tests

- Edit a syntax-colored file and reject results from an older document version.
- Scroll into a cold region and verify rendering remains responsive.
- Exercise structural coloring, smart indentation and folding.
- Exercise conservative fallback while reliable structural data is absent.
- Open a large file and verify budgeted viewport-first analysis.
- Confirm drawing consumes existing derived state without running analysis.
