# Design

Each entrypoint changes from `main()` to `main(argv: list[str] | None = None)` and passes `argv` to `ArgumentParser.parse_args`. A `None` value preserves command-line execution behavior. Tests call entrypoints with explicit lists.
