# Multi-position fatigue spectrum reports

`FatigueSpectrumReportBuilder` applies an ordered load spectrum independently at each selected FEM result position.

Each position input provides:
- an exact `ResultPosition`;
- an explicit, non-owning `FatigueMaterial` pointer;
- an ordered list of load blocks, each with its own result source, field name, projection and repetition count.

The builder assigns the row's position to every block. This prevents accidental mixing of different FEM locations within one spectrum. The block sources and material objects must remain alive for the duration of the call.

## Aggregation semantics

For position (p), the spectrum damage is:

[
D_p = sum_b r_b D_{p,b}
]

where (D_{p,b}) is the Palmgren-Miner damage of block (b) at position (p), and (r_b) is its repetition count.

**Damage is not summed across FEM positions.** Each position represents a distinct material point and has its own damage and spectrum life. The report only identifies the maximum computed damage among successful positions.

Failed positions remain in the report with `hasDamage=false`. Missing material, empty block lists, and failed block analyses are not represented as zero damage. Overall status distinguishes complete success, partial failure and total failure.

## Scope

The implementation uses the existing uniaxial projected S-N analysis and linear Palmgren-Miner rule. It does not concatenate block time histories, infer material assignments, apply mean-stress correction, or model sequence effects.
