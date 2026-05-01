# Controlled C++20 Code Style

## Principle

This project uses a controlled C++20 subset.

Optimize for a systems programmer reading the code locally:
- explicit control flow,
- concrete types,
- visible ownership,
- direct debugging,
- low indirection,
- domain-correct names.

Modern C++ idioms are not automatically improvements.

A shorter expression is a regression if it makes local behavior harder to understand.

## Preferred

Use:

- simple classes and structs,
- concrete enums,
- clear functions,
- explicit loops,
- RAII for real resource ownership,
- `std::string`, `std::vector`, `std::map`, `std::unordered_map`, `std::optional` when useful,
- `std::filesystem` for file-system checks where appropriate,
- existing project utilities,
- TVision-native mechanisms.

`auto` is acceptable for:
- iterators,
- obvious factory return values,
- cases where the explicit type is visually noisy and semantically irrelevant.

Otherwise prefer concrete types.

## Not allowed without explicit approval

Do not introduce:

- new templates,
- Concepts,
- type-erasure wrappers,
- visitor structures,
- `std::variant` dispatch for simple control flow,
- generic lambdas,
- deeply nested lambdas,
- STL algorithm chains when a loop is clearer,
- framework-like abstractions,
- new utility files,
- local one-off helpers,
- cross-calling mini-helper networks.

## Abstraction budget

The abstraction budget is zero by default.

A new abstraction is allowed only if it:

- removes real duplication in more than one place,
- names a stable domain concept,
- isolates platform or library friction,
- makes a long function materially easier to read.

Moving complexity into more files is not a reduction of complexity.

## Helper rule

A helper is rejected if it:

- wraps one or two local statements,
- hides simple control flow,
- exists only to make code look cleaner,
- forces the reader to jump between tiny functions,
- duplicates an existing project function,
- is named after mechanics instead of a domain concept.

When in doubt, keep the code inline and explicit.

## Naming

Names must describe what something is or does in domain terms.

Avoid:

- leading or trailing underscores,
- `Helper`,
- `Util`,
- `Manager`,
- `Handler`,
- `Choice` when the object is not a choice,
- `Action` when a more precise domain noun exists.

Longer names are acceptable when they are more precise.

## Control flow

- Prefer obvious `if`/`switch` logic.
- Do not replace readable branching with table-driven code unless the table is a real domain model.
- Explicit loops are preferred over clever transformations.
- Small one-line `if` statements are acceptable when they are clearer.

## Headers and sources

- Headers contain declarations.
- Implementations belong in `.cpp` files.
- Inline code in headers requires a technical reason.
- New source files require explicit approval.

## Comments

Comments are for contracts, invariants and non-obvious intent.
Do not add comments that merely narrate the next statement.
Do not write TODOs unless explicitly requested.
Do not describe broken architecture as a “hack” in code comments.
