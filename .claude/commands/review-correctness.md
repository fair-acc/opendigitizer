# Correctness Review

Baseline conventions are in CLAUDE.md — follow them, do not restate them.

## Focus

- Off-by-one errors in loops and bounds checks
- Iterator/pointer invalidation after container mutations
- Integer overflow in size/index calculations
- Undefined behaviour: signed overflow, null deref, uninitialized reads, aliasing violations
- Floating-point edge cases: NaN propagation, Inf, denormals, precision loss
- Race conditions in lock-free code (acquire/release ordering)
- `std::span` / `std::string_view` constructed from non-contiguous or temporary storage
- Implicit narrowing conversions that lose precision or sign
- Incorrect `processOne` / `processBulk` return semantics
- Tag propagation logic errors

## Output Format

For each finding:

```
**[CRITICAL|WARNING|NOTE]** `file:line` — description
> quoted code
Failure scenario: ...
```

Be specific. Only report genuine issues, not style preferences.

Review: $ARGUMENTS
