# Fatigue damage models

## Palmgren-Miner

The initial damage model implements linear cumulative damage for stress-life (S-N) data:

`D = sum_i(n_i / N_i)`

where (n_i) is the counted cycle weight (1 for a full cycle, 0.5 for a half cycle) and (N_i) is the cycles-to-failure value read from the S-N curve at the cycle amplitude and selected temperature.

Rainflow provides a peak-to-peak range. The model therefore evaluates the curve at:

`sigma_a = Delta_sigma / 2`

The mean stress stored in each Rainflow cycle is validated as finite but is deliberately not used. Mean-stress corrections must be explicit additional models or policies.

## Input and unit contract

The caller supplies:
- counted `LoadCycle` values;
- a validated `StressLifeSN` curve;
- the stress unit of the cycle ranges;
- an absolute temperature in kelvin.

The input unit must exactly match the curve unit (dimension, symbol, scale and offset). No automatic conversion occurs. The curve's own interpolation and no-extrapolation policy remains authoritative.

Zero-range cycles contribute no damage. Negative ranges, non-positive/non-finite counts, and non-finite means are rejected. Damage is summed with compensated (Kahan) summation.

## Output

The result reports cumulative damage for the supplied block and its nominal block repetition life:

`L_block = 1 / D_block`

when damage is positive. For an empty or zero-damage block, block life is positive infinity. This is a linear-rule estimate, not a prediction that accounts for sequence effects or material degradation.

## Explicit limitations

This implementation does not include:
- Goodman, Gerber, SWT or other mean-stress corrections;
- strain-life damage or cyclic plasticity;
- load-sequence effects or nonlinear damage;
- multiaxial damage criteria;
- temperature history within one block;
- safety factors or statistical scatter.

The interface `IFatigueDamageModel` allows other models to be introduced without changing cycle counting or material curve storage.
