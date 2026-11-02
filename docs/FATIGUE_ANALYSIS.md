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


## Strain-life damage: Coffin-Manson-Basquin, Morrow and SWT

The strain-life implementation is a separate uniaxial pipeline. It does not
reinterpret a stress Rainflow cycle as a strain cycle. The caller supplies a
cycle with total strain amplitude, signed mean stress, maximum stress and
Rainflow count, all associated with the same physical cycle.

The material model uses the Coffin-Manson-Basquin constants
\\(E,\\sigma_f',b,\\varepsilon_f',c\\), with
\\(E>0\\), \\(\\sigma_f'>0\\), \\(\\varepsilon_f'>0\\),
\\(b<0\\) and \\(c<0\\). The independent variable is reversals to
failure \\(2N_f\\), while Miner damage uses cycles to failure
\\(N_f=(2N_f)/2\\).

Baseline fully reversed relation:

\\[
\\varepsilon_a=\\frac{\\sigma_f'}{E}(2N_f)^b+
\\varepsilon_f'(2N_f)^c.
\\]

Morrow modifies the elastic Basquin term using the measured cycle mean stress:

\\[
\\varepsilon_a=\\frac{\\sigma_f'-\\sigma_m}{E}(2N_f)^b+
\\varepsilon_f'(2N_f)^c.
\\]

SWT uses the measured maximum stress and total strain amplitude as its
cycle parameter:

\\[
P_{SWT}=\\sigma_{max}\\varepsilon_a,
\\qquad
P_{SWT}(2N_f)=
\\frac{(\\sigma_f')^2}{E}(2N_f)^{2b}
+\\sigma_f'\\varepsilon_f'(2N_f)^{b+c}.
\\]

The inverse equation is solved by bisection in log10(reversals), over the
explicit interval `minimumReversals` to `maximumReversals` (defaults 1
to 1e16). No life extrapolation outside that interval is allowed. The
material stress unit must match the cycle stress unit exactly; the strain
unit must be dimensionless and match exactly. There is no implicit conversion.

### Important modelling boundaries

- The caller must provide stabilized cycle values. The model does not derive
  \\(\\sigma_m\\) or \\(\\sigma_{max}\\) from an elastic FE stress
  history and does not perform cyclic plasticity, Neuber/Glinka correction,
  or hysteresis-loop reconstruction.
- Morrow rejects cycles for which \\(\\sigma_f'-\\sigma_m\\leq0\\).
- SWT requires \\(\\sigma_{max}>0\\). It is a strain-life damage
  parameter, not the stress-life equivalent-amplitude approximation.
- The damage sum is linear Palmgren-Miner. Sequence effects and multiaxial
  critical-plane processing are outside this model.
- Morrow and SWT constants are material-condition dependent; temperature,
  environment and material direction must be represented by separately
  calibrated parameter sets until temperature-dependent parameter families
  are introduced.


## Local elastic-to-strain conversion: Neuber and Glinka

LocalStrainCorrection converts uniaxial elastic Rainflow cycles into local
stabilized strain-life cycle inputs. It accepts either the LoadCycle output
of scalar Rainflow directly, or an explicit ElasticStrainLifeCycle vector.
The Rainflow bridge preserves cycle count and source sample indices.

The cyclic stress-strain law is Ramberg-Osgood:

\[
\varepsilon_a=\frac{\sigma_a}{E}
+\left(\frac{\sigma_a}{K'}\right)^{1/n'}.
\]

For elastic FEM stress amplitude \(\sigma_{a,e}\), Neuber solves:

\[
\sigma_a\varepsilon_a=\frac{\sigma_{a,e}^2}{E}.
\]

Glinka instead equates the local cyclic strain energy density to the elastic
energy density:

\[
\int_0^{\sigma_a}\sigma\,d\varepsilon
=\frac{\sigma_{a,e}^2}{2E},
\]

where the left side is evaluated analytically from the Ramberg-Osgood law.
Both scalar equations are solved by bounded monotone bisection with finite
value checks.

The resulting StrainLifeCycle contains the local strain amplitude, the
supplied elastic mean stress, and a maximum stress approximated as
\(\sigma_m+\sigma_a\). This is deliberately an amplitude correction:
the mean stress is retained, not recalculated through a cyclic constitutive
history. Use these values as input to StrainLifeDamageModel.

### Scope and limitations

- This is a uniaxial local correction. It is not a multiaxial critical-plane
  method and does not support non-proportional loading.
- The cyclic Ramberg-Osgood constants must correspond to the material,
  temperature, orientation and cyclic stabilization condition being analysed.
- No residual stress relaxation, ratcheting, mean-stress relaxation,
  hysteresis-loop reconstruction, or incremental elastoplastic FE solution
  is performed.
- Neuber and Glinka are alternative engineering approximations; the caller
  must select the method appropriate to the notch/material problem.
- Stress units in Rainflow input and material parameters must match exactly.
  Strain units must be dimensionless. No conversion is performed.
- The conversion does not replace validation against strain-gauge data,
  notch analysis guidance, or a nonlinear FE solution where local plasticity
  is substantial.
