# CAD-to-FEM group export — Phase 3.5

This phase connects the backend-independent `MeshGroupRegistry` to the existing
Abaqus/CalculiX and Code_Aster mesh writers.

## Export mapping

| Generic mesh membership | Abaqus / CalculiX | Code_Aster |
|---|---|---|
| Group element IDs | `*ELSET` | `GROUP_MA` |
| Group node indices | `*NSET` named `<group>_NODES` | `GROUP_NO` named `<group>_NODES` |
| CAD source dimension | Comment metadata | Membership topology is represented by the selected element/node groups |
| CAD source entity IDs | Retained in OpenModel; not encoded in solver set syntax | Retained in OpenModel; not encoded in solver set syntax |

Node IDs and element IDs are emitted using the external identifiers stored in
`MeshData`; internal node indices are translated before writing.

## Avoiding duplicate sets

The meshers can expose the same CAD selection through both legacy physical/node
sets and the generic registry. Writers avoid emitting a second set when its name,
dimension and membership match an existing physical group or node set. A
different membership that sanitizes to an already-used Abaqus name is reported
as an export error rather than silently merged.

Abaqus/CalculiX set namespaces are checked independently for element and node
sets. Generic node memberships use the `_NODES` suffix to keep them distinct
from the element group with the CAD group's name.

Code_Aster group names are allocated through its existing 24-character
sanitizer/uniquifier. Element memberships become `GROUP_MA`; node memberships
become `GROUP_NO`. Existing matching `<group>_NODES` node sets are reused.

## Element topology

The Abaqus/CalculiX writer now accepts two-node line elements as `T3D2`, which
allows explicit CAD-edge line elements to be referenced by an element set.
Point elements are not added to the Abaqus writer in this phase; CAD vertices
remain represented by node-only groups. Code_Aster supports its existing
`SEG2` and `POI1` mesh element encodings.

## Important solver semantics

A node group is suitable for nodal constraints and nodal loads, but it is not a
substitute for a boundary-element group. Surface pressure, heat flux, convection
and similar distributed conditions must reference the appropriate surface
elements. Likewise, line loads require line elements where the solver formulation
supports them. This export phase preserves the memberships; it does not infer
or create missing boundary elements. CalculiX and Code_Aster analysis setup now
also resolve generic CAD groups: node selectors use the registry's explicit node
membership (or derive nodes from element membership when needed), and pressure
loads accept generic dimension-2 groups with actual surface element IDs. A
node-only face group is deliberately insufficient for pressure.

## Validation

The writer tests cover:
- CAD edge element memberships exported as `*ELSET` / `GROUP_MA`;
- CAD edge and vertex node memberships exported as `*NSET` / `GROUP_NO`;
- the `_NODES` naming convention;
- preservation of existing legacy physical groups and node sets without
  duplicate output.

The CI workflow is the authoritative compilation/test check for the branch.
