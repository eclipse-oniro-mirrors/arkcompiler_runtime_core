# Artifact-Size Measurement Discipline

Any byte-level or offset-level statistic used to justify an artifact-size conclusion (`.an`/`.abc` savings estimates,
section-growth numbers) MUST pass this checklist before its numbers are cited:

1. **Caliber check before statistics**: validate structural invariants over the full population first — entity
   leading fields (e.g. method entities must start with the magic number `0xaccadeca`), ordered non-overlapping
   ranges. A mis-aligned extraction window must fail here, not survive into conclusions.
2. **Dual-source cross-check**: extract the same data from two sources that share no code path (ELF symbol
   table vs `.aot` MethodHeaders via `ark_aotdump`; `readelf` vs a custom parser) and cross-validate. Accept
   only bitwise-identical results.
3. **Raw-tool recheck**: spot-check members with system tools (`cmp`, `objdump`), bypassing script digest/grouping
   logic.
4. **Negative testing**: include a pair expected to differ (near-identical but not equal) and confirm it is
   not merged (guards the comparison method against false positives).
5. **Multi-sample reproduction**: reproduce on self-compiled and build-system standard artifacts, on both target
   architectures.
6. **Report evidence with every number**: each cited figure names its validation method; data that cannot be
   cross-checked is labeled with a confidence level and must not be the sole basis for accept/reject decisions.

Precedent: a comparison window mis-aligned by 16 bytes (missing `CodePrefix`) once undercounted duplicate method
entities ~70x and nearly rejected a viable optimization; the same class of error also occurred in a first-pass
script and was caught by invariant checking. Full case and checklist:
`ark_aot_compact_size_reduction_glm53_0828.md` appendix "产物体积测量规范（交叉验证清单）".
