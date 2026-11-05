# Smaps analysis delta

## MODIFIED Requirements

### Requirement: Smaps parser exposes typed summary objects
The parser SHALL preserve immutable MemProfile and SmapsSummary objects exposing total and per-tag profiles. Profile fields SHALL remain size_kb, rss_kb, pss_kb, referenced_kb, shared_kb, private_kb, swap_kb, swap_pss_kb and anonymous_kb integers. Parsing SHALL return a summary when at least one mapping is aggregated, including zero-valued mappings, and None when none is aggregated.

#### Scenario: Matching mapping has zero values
- **WHEN** an included mapping has no nonzero supported metrics
- **THEN** the parser returns a zero-valued summary rather than None

#### Scenario: No mapping is aggregated
- **WHEN** text contains no mappings or all mappings are excluded
- **THEN** the parser returns None

#### Scenario: Caller parses smaps text
- **WHEN** parsing aggregates at least one mapping
- **THEN** it returns a SmapsSummary containing total and per-tag MemProfile objects

#### Scenario: Caller consumes profile fields
- **WHEN** a caller reads a MemProfile
- **THEN** all nine documented integer metric fields remain available

### Requirement: Smaps parser filters mappings by tag pattern
The system SHALL use a non-null compiled string regular expression for parse_smaps_text, defaulting to compiled `.*` when omitted. Explicit None, bytes patterns and non-pattern values SHALL be rejected. The pattern SHALL match each normalized tag using Pattern.match (start-anchored re.match semantics); whitespace SHALL be stripped and missing tags normalized to `[anonymous]`. Included mappings SHALL contribute to both totals and per-tag breakdowns. The parser SHALL remain logging-free and preserve existing supported-metric parsing, shared/private derivation, ignored unsupported or malformed metric lines, and zero defaults for absent metrics. No new content-integrity validation SHALL be inferred from structural evidence validation.

#### Scenario: Default includes every tag
- **WHEN** parsing is called with the default pattern
- **THEN** every mapping contributes exactly as unfiltered parsing did

#### Scenario: Start-anchored matching is used
- **WHEN** patterns `.*\.so` and `\.so$` are applied to `/system/lib64/lib.so`
- **THEN** the first matches and the second does not

#### Scenario: Anonymous mapping is matched
- **WHEN** a pattern matching `[anonymous]` is supplied
- **THEN** pathless mappings are included according to their normalized tag

#### Scenario: Explicit None is supplied
- **WHEN** a caller passes None instead of a compiled string pattern
- **THEN** the parser rejects it instead of treating it as an alternate unfiltered mode

#### Scenario: Parser aggregates all tags without a filter
- **WHEN** the caller omits the pattern argument
- **THEN** the default compiled `.*` aggregates every mapping; explicit None is not accepted

#### Scenario: Filter restricts breakdown and totals to matching tags
- **WHEN** a compiled pattern matches only some normalized tags
- **THEN** only those mappings contribute to both total and breakdown

#### Scenario: Filter matches no tags
- **WHEN** the compiled pattern matches no mapping
- **THEN** parsing returns None

#### Scenario: Parser returns None for text without mappings
- **WHEN** the input contains no mapping headers
- **THEN** parsing returns None rather than a zero summary

#### Scenario: Filter matching is anchored at the start of the tag
- **WHEN** `.*\.so` and `\.so$` are matched against `/system/lib64/lib.so`
- **THEN** only the first matches under Pattern.match semantics

#### Scenario: Filter applies to normalized tags
- **WHEN** a pattern is applied to a pathless mapping
- **THEN** it matches against the normalized `[anonymous]` tag
