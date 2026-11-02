# Rainflow cycle counting

## Scope

`CycleCounting::rainflow` implements scalar Rainflow counting on an explicitly projected stress or strain history. It is not a multiaxial cycle-counting method and must not be presented as one.

## Projection

For a normalized direction vector `n` in the history frame, the scalar channel is

`q(t) = n^T T(t) n`

where `T(t)` is the complete prepared stress or strain tensor. The direction must be finite and unit length. The caller is responsible for selecting a physically meaningful direction; this API does not search critical planes.

## Cycle output

Each `LoadCycle` stores:
- start and end turning points, including time and original history sample index;
- range and mean of the projected scalar;
- count weight: 1.0 for a closed cycle and 0.5 for a residual half cycle.

Plateaus are collapsed while retaining the last time/index for that plateau. Reversals are extracted before the three-point stack procedure. Remaining stack segments are emitted as half cycles.

## Multiaxial data

The original `StressHistory` remains unchanged and retains all tensor components. Scalar projection is an explicit derived view only. No von-Mises-only or principal-stress-only cycle count is inferred. Multiaxial criteria (critical plane, invariant-based, path-dependent) require a separate interface and algorithm, and are intentionally not implemented by this scalar Rainflow class.

## Limitations

This first implementation is a scalar turning-point Rainflow counter. It does not perform filtering, hysteresis thresholding, endpoint closure, time resampling, or damage accumulation. It preserves cycle endpoint sample references so later models can retrieve the source tensors.
