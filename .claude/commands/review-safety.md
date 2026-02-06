# Safety Review

Baseline conventions are in CLAUDE.md — follow them, do not restate them.

## Focus

**Memory Safety**

- Use-after-free (especially `std::span` / `std::string_view` from temporaries)
- Dangling references from expired lifetimes
- Buffer overflows / out-of-bounds access
- Uninitialized memory reads
- Double-free scenarios

**Thread Safety**

- Data races on shared mutable state
- Memory ordering issues (relaxed vs acquire/release on atomics)
- Lock ordering violations (potential deadlocks)
- False sharing on adjacent cache lines

**Real-Time Constraints**

- Blocking operations in `processOne` / `processBulk` (I/O, locks, unbounded allocation)
- Non-deterministic execution time in processing callbacks
- Priority inversion risks

**Exception Safety**

- Resource leaks on throw paths (relevant for user-facing code that may throw)
- Our library code must be exception-free — flag any `throw` in framework code

## Output Format

For each finding:

```
**[CRITICAL|WARNING|NOTE]** `file:line` — description
> quoted code
Risk: what can fail and under which conditions
```

Review: $ARGUMENTS
