# Spec Delta

## ADDED Requirements

### Requirement: Metadata models reject coercible Python values
Evidence and view metadata validation SHALL reject incompatible builtin Python values rather than coercing them. It SHALL continue to accept valid serialized JSON evidence and view metadata, including serialized path values used by the existing metadata format.

#### Scenario: Direct metadata construction receives a coercible value
- **WHEN** evidence or view metadata construction receives a numeric string for an integer field, an integer for a boolean field, or another incompatible builtin value
- **THEN** validation fails without producing or modifying metadata

#### Scenario: Valid metadata JSON is read
- **WHEN** a reader loads evidence or view metadata JSON that satisfies the existing format and field types
- **THEN** it accepts the metadata and preserves the existing reader behavior
