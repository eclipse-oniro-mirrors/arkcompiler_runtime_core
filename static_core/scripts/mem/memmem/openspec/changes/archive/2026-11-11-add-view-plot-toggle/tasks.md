# Tasks

## 1. Expose plot policy

- [x] 1.1 Add default-enabled `--plots`/`--no-plots` to shared view arguments and propagate it through both CLIs; verify parser and entrypoint tests cover defaults and disabled forwarding.
- [x] 1.2 Extend public `create_view` and the report writer with the plotting policy; verify facade tests cover `plots=False` and reject nonboolean values.

## 2. Persist and validate plot policy

- [x] 2.1 Add required strict `plots` view metadata, keep eager empty `plots/`, and skip SVG generation when disabled; verify report and metadata tests cover enabled, disabled, and empty views.
- [x] 2.2 Update strict view-reader fixtures and validation to require a boolean `plots` field while excluding it from equivalence; verify missing/nonboolean metadata is rejected and mixed plotting policies compare successfully.

## 3. Verify

- [x] 3.1 Run `make pylint`, `make mypy`, and `make test` to validate the changed CLI, facade, metadata, and report behavior.
