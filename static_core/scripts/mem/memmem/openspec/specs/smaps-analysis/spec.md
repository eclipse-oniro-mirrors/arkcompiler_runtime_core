# smaps-analysis Specification

## Purpose
Defines parsing and reporting semantics for raw `/proc/<pid>/smaps` memory snapshots.

## Requirements

### Requirement: Smaps parser produces total memory profile
The system SHALL parse raw `/proc/<pid>/smaps` text and produce total Size, Rss, Pss, Referenced, Shared, Private, Swap, SwapPss, and Anonymous memory values in KiB.

#### Scenario: Smaps file contains multiple mappings
- **WHEN** the parser receives smaps text with multiple memory mappings
- **THEN** the returned total profile contains the sum of Size, Rss, Pss, Referenced, Shared, Private, Swap, SwapPss, and Anonymous values across all mappings

#### Scenario: Smaps mapping omits a supported metric
- **WHEN** a mapping does not contain one of Size, Rss, Pss, Referenced, Shared, Private, Swap, SwapPss, or Anonymous
- **THEN** the parser treats the missing metric as zero for that mapping

#### Scenario: Smaps file contains unsupported metrics
- **WHEN** the parser receives smaps text with fields other than Size, Rss, Pss, Referenced, Shared, Private, Swap, SwapPss, and Anonymous
- **THEN** the parser ignores unsupported fields for the total profile

### Requirement: Smaps parser derives shared and private totals
The system SHALL calculate Shared as `Shared_Clean + Shared_Dirty` and Private as `Private_Clean + Private_Dirty` for each mapping.

#### Scenario: Mapping contains clean and dirty shared values
- **WHEN** smaps text contains `Shared_Clean` and `Shared_Dirty` values
- **THEN** the parser adds both values into the Shared metric

#### Scenario: Mapping contains clean and dirty private values
- **WHEN** smaps text contains `Private_Clean` and `Private_Dirty` values
- **THEN** the parser adds both values into the Private metric

### Requirement: Smaps parser produces per-tag breakdown
The system SHALL group parsed smaps mappings by mapping tag and produce a memory profile for each tag.

#### Scenario: Multiple mappings share a tag
- **WHEN** smaps text contains multiple mappings with the same tag
- **THEN** the breakdown entry for that tag contains the sum of supported metrics across those mappings

#### Scenario: Mappings have different tags
- **WHEN** smaps text contains mappings with different tags
- **THEN** the breakdown contains a separate memory profile for each tag

#### Scenario: Mapping has no explicit path tag
- **WHEN** a smaps mapping header has no explicit mapped path
- **THEN** the parser assigns it a stable tag suitable for reporting

### Requirement: Smaps parser exposes typed summary objects
The parser SHALL preserve immutable MemProfile and SmapsSummary objects exposing total and per-tag profiles. Profile fields SHALL remain size_kb, rss_kb, pss_kb, referenced_kb, shared_kb, private_kb, swap_kb, swap_pss_kb and anonymous_kb integers. Parsing SHALL return a summary when at least one mapping is aggregated, including zero-valued mappings, and None when none is aggregated.

#### Scenario: Matching mapping has zero values
- **WHEN** an included mapping has no nonzero supported metrics
- **THEN** the parser returns a zero-valued summary rather than None

#### Scenario: No mapping is aggregated
- **WHEN** text contains no mappings or all mappings are excluded
- **THEN** the parser returns None

#### Scenario: Caller parses smaps text
- **WHEN** a caller invokes `parse_smaps_text` with raw smaps text
- **THEN** the system returns a `SmapsSummary` with a total `MemProfile` and a tag breakdown dictionary

#### Scenario: Caller consumes profile fields
- **WHEN** a caller reads a returned `MemProfile`
- **THEN** the profile exposes `size_kb`, `rss_kb`, `pss_kb`, `referenced_kb`, `shared_kb`, `private_kb`, `swap_kb`, `swap_pss_kb`, and `anonymous_kb` integer fields

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
- **WHEN** a caller invokes `parse_smaps_text` without a tag filter
- **THEN** the parser aggregates every mapping into the breakdown and total profile exactly as before

#### Scenario: Filter restricts breakdown and totals to matching tags
- **WHEN** a caller passes a compiled pattern to `parse_smaps_text` and some mapping tags match via `re.match`
- **THEN** the breakdown contains only matching tags and the total profile contains the sum of the matching tags only

#### Scenario: Filter matches no tags
- **WHEN** a caller passes a compiled pattern to `parse_smaps_text` and no mapping tag matches via `re.match`
- **THEN** the parser returns `None`

#### Scenario: Parser returns None for text without mappings
- **WHEN** a caller invokes `parse_smaps_text` with text containing no mapping headers
- **THEN** the parser returns `None` rather than a zero-valued summary

#### Scenario: Filter matching is anchored at the start of the tag
- **WHEN** a caller passes the compiled pattern `.*\.so` against a mapping whose tag is `/system/lib64/lib.so`
- **THEN** the mapping is included, and a pattern such as `\.so$` that is not anchored at the start matches no tags

#### Scenario: Filter applies to normalized tags
- **WHEN** a caller passes a compiled pattern matching the normalized anonymous tag `[anonymous]`
- **THEN** mappings without an explicit path are included when their normalized tag matches and excluded otherwise
