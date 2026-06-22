# Controlled C++20 / C++18-Style Code Style

## Principle

This project builds as C++20 but defaults stylistically to classic C++18-style code.
C++20 features are allowed only where they carry a concrete technical benefit.

Optimize for a systems programmer reading the code locally:
- explicit control flow,
- concrete types,
- visible ownership,
- direct debugging,
- low indirection,
- domain-correct names.

Modern C++ idioms are not automatically improvements.

A shorter expression is a regression if it makes local behavior harder to understand.

## C++20 boundary

C++20 syntax is not a style target by itself. Prefer C++18-era code when the result
is equally clear and efficient.

Allowed C++20 exceptions:

- `std::jthread` and `std::stop_token` for existing cooperative cancellation and background work,
- `std::span` where it avoids copying and represents a bounded view explicitly,
- `std::string_view` for non-owning text parsing and high-volume string analysis,
- `std::filesystem` for path and directory operations,
- standard containers and algorithms when they express the actual data structure or avoid hand-written fragile code,
- `starts_with` / `ends_with` in syntax, folding, indentation and other text-heavy recognizers where replacing them would add noise without measurable benefit.

Avoid introducing C++20 features just to shorten code. In particular, do not add
concepts, ranges pipelines, generic algorithm chains, or defaulted comparisons
unless the touched contract explicitly benefits from them.

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
- Use `switch` for closed enum-like decision axes when it makes the alternatives explicit.
- Use descriptor tables when several operations read the same stable domain mapping.
- Do not use table-driven code to disguise incidental branching or geometry checks.
- Explicit loops are preferred over clever transformations.
- Small one-line `if` statements are acceptable when they are clearer.

## Headers and sources

- Header files describe the public contract of a translation unit.
- Source files contain implementation details.
- Do not use the old C rule “headers contain everything that does not allocate storage”.
- A declaration belongs in a header only when another translation unit must name it.
- Local descriptor structs, mapping tables, private constants and file-local helper functions belong in `.cpp` files, preferably in an anonymous namespace.
- Public enums and structs belong in headers only when they are part of the callable API.
- Private class members may be unavoidable in headers because they define object layout; do not add more private surface than the class needs.
- Inline code in headers requires a technical reason, such as templates or required trivial inline definitions.
- New source files require explicit approval.

## Comments

Comments are for contracts, invariants and non-obvious intent.
Do not add comments that merely narrate the next statement.
Do not write TODOs unless explicitly requested.
Do not describe broken architecture as a “hack” in code comments.
