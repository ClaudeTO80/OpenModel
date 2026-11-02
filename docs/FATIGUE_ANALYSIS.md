# Fatigue load-spectrum analysis

## Purpose

`FatigueSpectrumAnalysis` evaluates a sequence of independent load histories at one FEM result position and accumulates their Palmgren-Miner damage. Every block is passed through the existing `FatigueAnalysis` pipeline (exact source samples, tensor preparation, directional projection, Rainflow, S-N evaluation and Miner accumulation).

## Block contract

A `FatigueLoadBlockInput` contains:
- a non-owning pointer to an `IResultSource` (the source must outlive the call);
- an exact result field name and `ResultPosition`;
- an explicit unit projection direction;
- an optional label;
- a positive finite `repetitions` count.

Each block is a separate time history. Its time may restart at zero; blocks are never concatenated and no synthetic time axis is invented. The input vector order is retained in the result and represents the declared operating sequence. The caller must ensure that the block definitions and repetitions describe the actual operating spectrum.

## Damage accumulation

For block (b), the inner Rainflow/Miner analysis returns:

[
D_b = \sum_{i\in b}\frac{n_i}{N_i}.
]

If that block occurs (r_b) times in one complete spectrum repetition, its contribution is (r_bD_b). The spectrum damage and nominal spectrum life are:

[
D_{\mathrm{spectrum}}=\sum_b r_bD_b,
\qquad
L_{\mathrm{spectrum}}=\frac{1}{D_{\mathrm{spectrum}}}.
]

A zero-damage spectrum has infinite nominal life. Accumulation checks for non-finite overflow.

## Results and traceability

The result contains one `FatigueLoadBlockResult` per successfully evaluated block, preserving its label, repetition count, individual damage and full `FatigueAnalysisResult`. Thus cycle endpoints remain traceable to the original samples and source metadata. On failure, the index of the failing block and a diagnostic are returned; preceding successful blocks remain available.

## Scope

This is linear Palmgren-Miner accumulation. For this model, block order does not change the numerical total, although it is retained as part of the input contract for reporting and future sequence-sensitive models. No sequence effect, overload interaction, residual strength, mean-stress correction, automatic material mapping, unit conversion, or multiaxial criterion is introduced.

All blocks in one spectrum use the same selected material, temperature and target coordinate frame. Each source history must independently satisfy the strict time-history contract.


## Uniaxial mean-stress corrections

The S-N damage pipeline can optionally correct each counted uniaxial cycle before
looking up its life. The default is `MeanStressCorrectionKind::None`; the
correction is explicitly selected in `FatigueDamageInput`. All cycle stresses
and material strengths must use exactly the same unit as the S-N curve. No
automatic conversion or tension-only clipping is performed.

For stress amplitude \\(\\sigma_a\\), mean stress \\(\\sigma_m\\), maximum
stress \\(\\sigma_{max}=\\sigma_m+\\sigma_a\\), ultimate strength
\\(S_u\\), yield strength \\(S_y\\), and true fracture strength
\\(S_f\\), the implemented stress-life transforms are:

- Goodman: \\(\\sigma_{a,c}=\\sigma_a/(1-\\sigma_m/S_u)\\).
- Gerber: \\(\\sigma_{a,c}=\\sigma_a/[1-(\\sigma_m/S_u)^2]\\).
- Soderberg: \\(\\sigma_{a,c}=\\sigma_a/(1-\\sigma_m/S_y)\\).
- Morrow: \\(\\sigma_{a,c}=\\sigma_a/(1-\\sigma_m/S_f)\\).
- Stress-life SWT equivalent amplitude: \\(\\sigma_{a,c}=\\sqrt{\\sigma_a\\sigma_{max}}\\).

Goodman and Gerber require ultimate tensile strength; Soderberg requires yield
strength; Morrow requires true fracture strength (or a deliberately calibrated
equivalent reference strength). Missing strengths and singular/non-positive
denominators are errors, not silently clamped values. Gerber is symmetric in
the sign of mean stress, unlike the linear Goodman/Soderberg/Morrow relations.

**SWT qualification:** the original Smith-Watson-Topper parameter is a
strain-life parameter based on \\(P_{SWT}=\\sigma_{max}\\varepsilon_a\\).
The transform exposed here is its common stress-life equivalent-amplitude
form, not a full strain-life SWT solver. It rejects cycles with non-positive
maximum stress. A proper strain-life SWT implementation needs a compatible
cyclic stress-strain/strain-life material model and must solve the SWT life
equation; it is intentionally not inferred from the existing tabulated
strain-amplitude curve.

These corrections are uniaxial cycle-level methods. They are not applied to
multiaxial tensor paths or Wang-Brown reversals, and they do not replace a
critical-plane mean-stress treatment. The resulting corrected amplitude is
passed to the existing S-N curve; Palmgren-Miner accumulation remains linear.
