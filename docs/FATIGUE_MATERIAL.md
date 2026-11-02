# Fatigue material and tabulated curve families

## Scope

`FatigueMaterial` stores material identity and fatigue curve families. It does not perform cycle counting, mean-stress correction, damage accumulation, or solver-specific result interpretation.

Two curve families are supported:
- **S-N** (`StressLifeSN`): stress amplitude versus cycles to failure.
- **Strain-life** (`StrainLifeEN`): strain amplitude versus cycles to failure.

The ordinate is cycles to failure, not reversals. The abscissa is amplitude, not peak-to-peak range. Conversion from a counted range to amplitude belongs to a separately specified fatigue method.

## Units and temperature

S-N requires a declared stress dimension, non-empty unit symbol and known positive scale to SI. Strain-life requires a dimensionless unit with known positive scale. No implicit unit conversion occurs during evaluation: input amplitude must use the exact unit stored by the curve.

Temperature is absolute kelvin. A curve family may contain multiple strictly increasing temperature slices. Temperature interpolation is linear in `log10(N)`; amplitude interpolation is linear in log-log space. For an intermediate temperature, amplitude must lie in the intersection of the bracketing slices' amplitude ranges.

## Validation and boundaries

Each slice requires at least two points. Amplitudes must be positive and strictly increasing; cycles to failure must be positive and strictly decreasing. Temperatures must be finite, positive and unique.

Extrapolation is disabled. Evaluation outside the tabulated temperature or amplitude domain returns a status and diagnostic. No silent endpoint clamping is performed.

## Deliberate non-goals

The curve data does not prescribe:
- stress ratio (R), mean-stress correction (Goodman, Gerber, SWT, etc.);
- surface, size, notch, reliability or manufacturing factors;
- environment, frequency, dwell-time or sequence effects;
- interpolation rules beyond the declared log-log and temperature interpolation;
- Palmgren-Miner or nonlinear damage accumulation.

These must be modeled as explicit policies or separate components, not hidden inside material curve lookup.
