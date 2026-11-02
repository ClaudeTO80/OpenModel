# Fatigue sample preparation

`FatigueSamplePreparer` is the policy boundary between the raw `IResultSource` and fatigue algorithms.

- It requests one exact field, external entity identity and step from the source.
- It retains the original `ResultSample` for traceability.
- It rejects averaged samples by default. Interpolated and extrapolated samples are also rejected by default; these policies can be explicitly relaxed.
- It accepts stress and strain tensors only when their declared physical dimensions are compatible. Unknown dimensions remain permitted but are not inferred. Known stress dimensions must be pressure (M L^-1 T^-2); known strain dimensions must be dimensionless.
- It delegates label mapping, shear conversion and coordinate rotation to `TensorNormalizer`.
- It provides principal values for both stress and strain, and von Mises equivalent stress only for stress.
- Vector preparation supports displacement, velocity, acceleration, force and reaction force.

No unit scale conversion is performed. Samples from different unit systems must be converted by an explicit upstream policy before comparison or aggregation. The raw source adapter remains unchanged and does not enforce these fatigue-specific policies.
