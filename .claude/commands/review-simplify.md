# Simplification Review

Baseline conventions are in CLAUDE.md — follow them, do not restate them.

## Focus

- Raw loops replaceable with `std::ranges` / `std::algorithms`
- Nested `if`/`else` collapsible with early returns or guard clauses
- Verbose patterns replaceable with structured bindings
- Manual tuple access replaceable with `auto [a, b] = ...`
- `std::function` replaceable with a template (no type erasure needed)
- Recursive templates replaceable with fold expressions
- Explicit constructors replaceable with aggregate / designated initialisation
- `constexpr if` eliminating dead branches
- Named lambdas extractable from complex inline expressions
- Over-engineered abstractions: wrappers, factories, or hierarchies with a single implementation
- Unnecessary template parameters that could be concrete types

Do NOT suggest changes that reduce debuggability, obscure algorithmic intent,
or degrade compile-time error quality.

## Output Format

For each finding:

```
**[CRITICAL|WARNING|NOTE]** `file:line` — description
> original code
Simplification: concrete replacement (or brief description if large)
```

Review: $ARGUMENTS
