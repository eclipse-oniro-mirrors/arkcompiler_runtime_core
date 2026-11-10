# Tasks

## 1. Refactor Footprint Model

- [x] 1.1 Add `Footprint`, inferred `FOOTPRINT_METRICS`, and a `MemProfile` conversion helper in `src/profile.py`; cover derived source values and metric order.
- [x] 1.2 Refactor footprint report generation to aggregate `Footprint` fields without hardcoded metric mappings; preserve CSV output.
- [x] 1.3 Update affected tests and run `make pylint`, `make tests_full`, `git diff --check`, and strict OpenSpec validation.

## 2. Centralize Memdump Categories

- [x] 2.1 Define one ordered memdump category-to-pattern source in `src/report.py`; derive category output order and classification from it while preserving first-match behavior and the `other` fallback.
- [x] 2.2 Cover preserved category order, an overlapping category match, and fallback classification; run `make pylint`, `make tests_full`, `git diff --check`, and strict OpenSpec validation.
