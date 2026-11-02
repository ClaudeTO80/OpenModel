# Multi-position fatigue reports

## Purpose

`FatigueReportBuilder` evaluates one named stress field at an explicit collection of FEM result positions. Each row runs the complete uniaxial S-N workflow independently and retains its full `FatigueAnalysisResult`.

## Explicit material assignment

Every `FatiguePositionInput` contains a non-owning pointer to its `FatigueMaterial`, a projection direction and a `ResultPosition`. The caller owns the material objects for the duration of the call. No material is inferred from element IDs, mesh regions or solver input decks.

The same API supports nodal, element and native integration-point locations through `IResultSource`. An integration-point position includes its parent element ID and solver integration-point number. The adapter's exact lookup rules apply; there is no fallback to element averages.

## Result semantics

Each row has an independent `hasDamage` flag. This is essential: a failed or missing analysis is not a zero-damage result.

The report summarizes:
- successful and failed positions;
- successful zero-damage and positive-damage positions;
- maximum computed damage and the index of its row;
- per-position diagnostics and full cycle/history traceability.

Overall status is `Success` when every row succeeds, `PartialFailure` when at least one row succeeds and at least one fails, and `AllPositionsFailed` when none succeeds. Empty selection and invalid field input have separate statuses.

The maximum-damage index refers to the original input order in `positions`. It is meaningful only when `hasMaximumDamage` is true.

## Scope

This report currently handles one source, one field name and one temperature per invocation. Each position may use a different explicitly assigned material and projection direction. It reports the scalar projected uniaxial damage; it does not claim to be a multiaxial fatigue criterion.

The material pointers are non-owning and must remain valid during the call. The result copies analysis data and does not retain those pointers.
