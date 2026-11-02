# End-to-end fatigue analysis

## Workflow

`FatigueAnalysis::run` orchestrates one solver-independent, uniaxial stress-life analysis at one FEM result position:

1. Validate the selected `FatigueMaterial` and locate its S-N curve.
2. Build a time-ordered `StressHistory` from exact field/position/step samples.
3. Normalize tensor components and coordinate frame using the existing preparation layer.
4. Project the full stress tensor onto the caller-supplied unit direction (q(t)=n^T\sigma(t)n).
5. Count scalar cycles with Rainflow.
6. Evaluate the S-N curve at half the Rainflow range and accumulate Palmgren-Miner damage.

## Traceability

The returned `FatigueAnalysisResult` retains:
- field name and FEM result position;
- source metadata (solver, format, version, fingerprint when supplied);
- the complete normalized stress history and original samples;
- cycle turning-point times and source history indices;
- cycle ranges, means and weights;
- the selected projection direction, temperature and damage summary.

The caller can therefore inspect each counted cycle and follow its endpoints back to the original FEM samples. No solver-specific logic is introduced in this service.

## Scope and assumptions

This is explicitly a uniaxial S-N workflow. The projection direction is expressed in the selected history frame and must be a finite unit vector. It is not a multiaxial fatigue criterion.

Material assignment is explicit: the caller supplies the material for the requested FEM position. The service does not infer element materials from a mesh or solver deck.

No unit conversion is performed. Stress history and S-N curve units must match exactly. Temperature is one constant absolute value in kelvin for the complete history. Mean stress is retained in Rainflow cycles but not corrected in Palmgren-Miner.

The history builder currently requires every source step to be explicitly time-valued and strictly increasing. Static load sequences and multiple blocks need a separate explicit sequence model; they are not synthesized from step indices.

## Failure handling

The result status identifies the failed stage (material, history, quantity, cycle counting or damage evaluation) and includes a diagnostic. Partial history/cycle results are retained where they have already been built, allowing inspection without treating a failed analysis as successful.
