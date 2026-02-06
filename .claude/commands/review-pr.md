# Pull Request Review

Baseline conventions are in CLAUDE.md — follow them, do not restate them.

Perform a concise review covering correctness, performance, API design, safety,
simplification opportunities, and test coverage.

## Output Format

```
## Summary
One paragraph: what this PR does and its overall state.

## Critical Issues
[CRITICAL] findings that must be addressed. Omit section if none.

## Warnings
[WARNING] findings worth addressing. Omit section if none.

## Notes
[NOTE] minor improvements. Omit section if none.

## Test Gaps
Missing coverage or test quality concerns. Omit section if none.
```

Quote exact lines (`file:line` + code) for every finding.
Keep total response under 500 words unless critical issues require elaboration.
Do not produce a merge verdict — the human decides.

Review: $ARGUMENTS
