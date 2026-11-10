# Design

## Decision

Define frozen `Footprint` beside `MemProfile` in `src/profile.py`, with `footprint` and `private_clean_kb` fields. Infer `FOOTPRINT_METRICS` from its dataclass fields and provide a conversion helper from `MemProfile`.

Report generation will aggregate converted `Footprint` instances through the same field iteration used by other profile-based reports. This preserves the footprint CSV and comparison schemas.

Define an insertion-ordered mapping of memdump category names to regular-expression patterns in `src/report.py`, including `other` with no patterns. Derive `MEMDUMP_CATEGORIES` from its keys and classify by returning the first category whose pattern matches. If no pattern matches, return `other`. Retain the current category and pattern order so a tag matching more than one category keeps its existing first-match result.

## Risks

- Metric order becomes dataclass-field order; retain the existing field order.
- Derived footprint must remain `pss_kb + swap_pss_kb`; cover conversion directly in tests.
- Regular expressions must preserve the prior prefix, suffix, and exact-match semantics; cover an overlapping `.hap`/`.so` tag and the fallback category.
