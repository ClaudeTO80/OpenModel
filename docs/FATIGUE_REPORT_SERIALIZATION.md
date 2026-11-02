# Fatigue report serialization

`FatigueReportWriter` writes dependency-free CSV and JSON to caller-owned streams. It does not open files or choose paths.

## CSV

- `writeCsvSummary`: one row per selected FEM position, including failed rows. Missing damage/life/cycle counts are empty fields, never zero.
- `writeCsvCycles`: one row per counted cycle for successful positions. Position identity and source sample indices are retained.

Use separate files for summary and cycle detail. Cycle detail can be substantially larger than the position summary.

## JSON

`writeJson(report, stream, includeCycles)` emits schema `openmodel.fatigue-report.v1`. The document contains report status, field, source identity, summary counters and one object per selected position. Failed rows remain present with `has_damage=false` and null numeric damage/life fields. Non-finite numeric values are encoded as JSON null.

When `includeCycles=true`, each successful position contains its counted cycles. The default omits cycle arrays to keep routine reports compact.

## Stability and limitations

The schema is explicitly versioned. JSON strings are escaped, output uses the classic numeric stream representation supplied by the caller's stream locale, and no third-party JSON dependency is required. The writer does not serialize raw tensor histories in this version; those remain accessible from each in-memory `FatigueAnalysisResult`.
