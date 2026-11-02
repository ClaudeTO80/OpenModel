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
