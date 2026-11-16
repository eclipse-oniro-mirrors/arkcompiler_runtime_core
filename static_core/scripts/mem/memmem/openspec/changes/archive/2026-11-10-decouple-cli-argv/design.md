# Design

## Context

The five executable modules currently accept `list[str] | None`, allowing `argparse` to read `sys.argv` when callers omit arguments. See `proposal.md` for motivation.

## Goals / Non-Goals

**Goals:**
- Keep process argument access in each executable boundary.
- Make every direct entrypoint invocation explicit and deterministic.

**Non-Goals:**
- Change command-line parsing, exit statuses, or subprocess invocation behavior.

## Decisions

- Change each CLI entrypoint to `main(argv: list[str])` and pass `argv` directly to `parse_args`. This removes the implicit process-global fallback. Retaining an optional default was rejected because it leaves the coupling in place.
- Have every `if __name__ == "__main__"` block call `main(sys.argv[1:])`. This preserves command-line behavior while making the boundary explicit.
- Update direct-call tests to provide lists, including an empty list for no-option recorder coverage. Subprocess tests remain unchanged because they validate executable behavior.

## Risks / Trade-offs

- Direct external callers omitting `argv` will receive a `TypeError` → This intentional breaking change is documented in the proposal and spec delta.
- A CLI module could be missed → Search all project entrypoints and direct `main` test calls, then run the full suite.

## Migration Plan

1. Update the five entrypoints and their executable boundaries.
2. Update direct tests to pass lists.
3. Run formatting, tests, and strict OpenSpec validation. Rollback reverts the small API migration as one change.
