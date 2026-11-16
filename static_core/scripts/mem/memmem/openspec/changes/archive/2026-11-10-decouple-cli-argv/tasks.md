# Tasks

## 1. Decouple CLI Entry Points

- [x] 1.1 Require `list[str]` in all five CLI `main` signatures and pass `sys.argv[1:]` from every executable boundary; verify no optional CLI argv signatures remain.
- [x] 1.2 Update every direct CLI test invocation to supply an argument list; verify no production CLI `main()` call without arguments remains in tests.

## 2. Validate Migration

- [x] 2.1 Run the CLI and recorder tests plus the full test suite, static checks, diff check, and strict OpenSpec validation; verify command-line subprocess behavior remains covered.
