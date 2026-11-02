# Fatigue result-source contract

## Scope

The fatigue module consumes solver-neutral, in-memory samples through
`OpenModel::Fatigue::IResultSource`. It does not parse CalculiX, Code_Aster,
Abaqus or other solver files. Solver-specific importers populate OpenModel
result models; adapters expose those models through this interface.

## Identity and indexing

- `stepIndex` is a zero-based index into the source's ordered history.
- Node and element identifiers are the external solver/model IDs, never array
  offsets or mesh-local indices.
- Integration-point positions identify the parent element plus a 1-based
  integration-point number and, when applicable, a 1-based section-point number.
- An IP request must never be answered with an element average or nodal value.

## Step semantics

Every step carries a numeric parameter, its semantic kind
(`Time`, `Frequency`, `LoadFactor`, `None`, or `User`), a label and an
optional mode number. A fatigue history must not treat frequency or eigenmode
indices as chronological load time.

## Tensor and component semantics

Each result field carries a `ResultFieldSemantics` record:

- quantity kind (stress, strain, displacement, temperature, force, ...);
- component shape (scalar, vector, symmetric tensor, full matrix);
- coordinate frame (global, local, material, unknown), plus an optional per-record orthogonal basis mapping local axes into global axes;
- shear convention (tensorial, engineering, not applicable, unknown);
- provenance flags (derived, averaged, interpolated, extrapolated).

Component labels and values always retain importer order. No universal Voigt
ordering is imposed. Algorithms must map labels explicitly and validate that
the requested tensor components exist. A six-component tensor is not assumed
to be valid merely because it has six numbers.

For strain, the shear convention is essential: engineering shear
`gamma_xy = 2 epsilon_xy` differs from tensor shear `epsilon_xy`. Algorithms
must convert to one internal convention before invariants or fatigue criteria
are evaluated. Unknown convention is a hard precondition failure for any
calculation that uses shear components. A non-global vector/tensor sample also
requires its per-record local-to-global basis before it can be combined with
samples in another frame. The basis stores its three local unit axes as columns
expressed in global coordinates; it contains no translation because translation
does not affect vector/tensor components.

## Physical dimensions and units

A `PhysicalUnit` contains SI base-dimension exponents (length, mass, time,
electric current, temperature, amount of substance, luminous intensity), a
display symbol, a multiplicative `scaleToSI` and an optional affine
`offsetToSI`. The dimension can be known while the unit scale remains unknown.
A zero scale means "not supplied"; it is never a conversion factor.

Examples of dimensions:
- displacement: L;
- stress/pressure: M L^-1 T^-2;
- force: M L T^-2;
- strain: dimensionless;
- temperature: Theta.

The importer must not invent the unit system. CalculiX result files do not
declare a coherent unit system, so the DAT/FRD importer records known physical
dimensions for recognized quantities but leaves `symbol` and `scaleToSI`
unknown. A caller may provide the actual model unit system when it is known.
No implicit SI assumption or conversion is allowed.

Solver/version, format, source name and model fingerprint may be supplied by
the caller. Provenance flags are positive assertions only: false means
"not reported", not proof that averaging or extrapolation never occurred.

## Missing and invalid data

`readSample` returns false for unavailable or ambiguous samples, invalid
record dimensions, duplicate external IDs, unsupported positions and
non-finite components. It does not return zero as a substitute for missing
data. Callers should retain diagnostics at import time and distinguish missing
data from physically zero values.

## Current implementation boundary

The adapter exposes nodal, per-element and native integration-point records.
The CalculiX DAT importer stores each `element + integration-point` row in
`FemResults::integrationPointFields`. For viewer compatibility it also keeps a
separate per-element unweighted mean in `fields`, with the `DAT_IP_AVERAGED`
diagnostic. Fatigue algorithms must request `IntegrationPoint` explicitly and
must not use the compatibility mean as a substitute. DAT currently does not
supply section-point identity, so it is stored as zero; formats that do supply
section points must populate that key.
Likewise, units, tensor semantics, material assignments and load-history
provenance must be populated by their owning model/import layers before
algorithms rely on them.

## Threading and lifetime

The adapter is non-owning and read-only. The referenced `FemResults` must
outlive it and must not be mutated concurrently. Independent adapters may read
the same immutable result model concurrently.
