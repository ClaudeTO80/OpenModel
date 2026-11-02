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

## Field components

The source returns component labels and values in exactly the same order as
stored by the importer. The contract does not assume a universal Voigt order.
A later tensor-normalization layer must map labels explicitly and must know
whether shear strain is tensorial or engineering. The current core FemResults
model does not yet carry this semantic metadata; the adapter therefore reports
`Unknown`, rather than guessing.

## Units and provenance

Unit strings are explicit metadata. Empty unit strings mean unknown; there is
no implicit SI assumption or conversion. Solver/version, format, source name
and model fingerprint may be supplied by the caller. Sample flags identify
derived, averaged, interpolated and extrapolated values when the source knows
that provenance. An unset flag is not proof that an operation never occurred.

## Missing and invalid data

`readSample` returns false for unavailable or ambiguous samples, invalid
record dimensions, duplicate external IDs, unsupported positions and
non-finite components. It does not return zero as a substitute for missing
data. Callers should retain diagnostics at import time and distinguish missing
data from physically zero values.

## Current implementation boundary

The adapter currently exposes the existing `FemResults` nodal and per-element
records. The current CalculiX DAT importer reduces integration-point rows to
one element record and emits a diagnostic; such a record is not eligible for
an integration-point fatigue calculation. Preserving native integration-point
records requires a follow-up extension to `FemResults` and importer storage.
Likewise, units, tensor semantics, material assignments and load-history
provenance must be populated by their owning model/import layers before
algorithms rely on them.

## Threading and lifetime

The adapter is non-owning and read-only. The referenced `FemResults` must
outlive it and must not be mutated concurrently. Independent adapters may read
the same immutable result model concurrently.
