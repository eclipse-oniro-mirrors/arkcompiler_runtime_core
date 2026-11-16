# Design

## Context

See proposal.md for motivation. `lib.py` is the public facade and already exposes its intended function surface through `__all__`; the three logger functions are defined in `src/log.py` and re-exported by that facade. The README describes API behavior, input constraints, logger levels, and evidence/view output ownership, but those details are not present at function definitions.

## Goals / Non-Goals

**Goals:**

- Add uniform developer docstrings to every public function export from `lib.py`.
- Document the logger helpers where they are defined while keeping their `lib.py` re-export behavior clear.
- Describe validation, result guarantees, and externally observable side effects without duplicating full output layouts.

**Non-Goals:**

- Document exported model and type constructors.
- Change function signatures, validation, output layouts, logger behavior, or public exports.
- Modify README content or introduce a generated API documentation system.

## Decisions

### 1. Use Doxygen-style docstrings inside public function definitions

Each docstring will contain `@brief`, one `@param` entry per parameter, and `@return`. As the first statement in each function, it is exposed through `__doc__` and surfaced by IDEs and Python documentation tools. This satisfies the requested developer-comment structure while retaining a concise Doxygen-style format.

### 2. Document behavior at definitions, not at facade imports

Docstrings for functions implemented in `lib.py` will live there. `configure_logger`, `get_logger`, and `reset_logger` will be documented in `src/log.py`, where their shared-state behavior is implemented. This keeps documentation co-located with behavior and prevents duplicate documentation at re-export sites.

### 3. Keep docstrings concise and link to README layouts

`execute_flow` and `create_view` will explicitly identify directory creation, exclusive destination ownership, partial-output retention, and device interaction where applicable. They will link to `README.md#evidence` or `README.md#view` for layout details. Builder docstrings will state that they create validated command or flow models and do not perform device or filesystem operations.

## Risks / Trade-offs

- [Docstrings drift from implementation] -> Keep docstrings limited to current signatures and behavior already covered by README and tests.
- [Docstrings become repetitive across builders] -> Use a concise shared pattern while retaining each builder's parameter constraints.
- [Links become stale after README heading changes] -> Use stable, existing heading anchors and keep layout details in the README.

## Migration Plan

No migration is required. The docstrings ship with the existing code and can be reverted without behavioral impact.
