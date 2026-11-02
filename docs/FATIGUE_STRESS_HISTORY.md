# Fatigue stress history

## Purpose

`StressHistory` stores the complete normalized tensor at one FEM result position over physical time. It is deliberately upstream of cycle counting and damage models.

## Invariants

- One history refers to exactly one `ResultPosition` (node, element, or integration point).
- All samples have the same quantity: stress or strain. Stress and strain histories are never mixed.
- Every source step must explicitly declare `StepParameterKind::Time`. A load factor, frequency, mode number, or source step index is not a time coordinate.
- Physical times must be finite and strictly increasing in source order. Duplicate timestamps and implicit sorting are rejected.
- Every tensor is prepared through `FatigueSamplePreparer`; the complete 3D tensor is retained in the requested frame.
- The original `ResultSample`, source step and source step index are retained for traceability.
- Unit metadata must be identical for all samples. The builder does not convert units or infer missing scales.

## API

```cpp
auto result = StressHistoryBuilder::build(
    source, "STRESS", integrationPoint, ReferenceFrame::Global);

if (!result) {
    // inspect result.status and result.diagnostic
}

const StressHistory& history = result.history;
for (const auto& sample : history.samples()) {
    const double time = sample.time;
    const Tensor3& stress = sample.prepared.tensor;
}
```

## Multiaxial information

The history stores all six independent components of a symmetric 3D tensor at every time point. Von Mises stress and principal values are derived quantities available from the prepared sample, but they do not replace the tensor. This is essential because multiaxial cycle counting and critical-plane methods need phase and direction information that scalar reductions discard.

## Scope and limitations

The current builder requires every source step to contain the requested sample. It does not interpolate missing records, merge repeated timestamps, resample nonuniform histories, or convert units. Those operations must be explicit preprocessing stages with their own provenance. The source's step ordering is preserved exactly.
