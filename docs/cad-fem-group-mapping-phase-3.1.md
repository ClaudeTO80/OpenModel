# CAD-to-FEM group mapping — Phase 3.1

## Goal

CAD groups must remain identifiable after meshing without depending on a specific
mesher's physical-group or marker implementation. The canonical representation is
the OpenModel mesh-group registry. Gmsh physical groups remain an adapter/export
representation, not the source of truth.

## Data model

`MeshGroup` stores:

- `name`: unique name in the mesh registry;
- `dimension`: topological dimension represented by the group (0–3);
- `source`: `UserDefined`, `CAD`, or `Imported`;
- `sourceEntityIds`: originating CAD entity identifiers, when available;
- `elementIds`: mesh element membership;
- `nodeIndices`: node membership.

The element and node lists are independent memberships. A node can belong to
several groups; this is required at shared edges and vertices of adjacent CAD
faces. The node list is currently populated as a derived convenience from the
mapped elements, while element membership remains the primary face/solid mapping.

## Mapping policy

| CAD entity | Primary FEM association | Derived association |
|---|---|---|
| Face (2D) | Boundary elements | Nodes on those elements |
| Solid (3D) | Volume elements | Nodes on those elements |
| Edge (1D) | Boundary line elements | Nodes on those elements |
| Vertex (0D) | Point elements / geometric node association | Node membership |

Phase 3.1 currently populates face and solid groups through the Gmsh adapter.
Edge and vertex mapping are deliberately left for the next increment; they need
explicit geometric matching and cannot safely be inferred from face node sets.

## Validation and invariants

- Registry names are unique.
- Group dimensions are in the range 0–3.
- Node indices must exist in the current mesh.
- Element IDs must exist and their topological dimension must match the group.
- Duplicate membership entries are removed while preserving first-seen order.
- Overlapping groups and shared nodes are valid.

The existing `MeshPhysicalGroup` model is retained for compatibility with current
solver writers and mesher adapters. During migration, a CAD-derived group is
represented both in the generic registry and, where required, as a physical group.

## Current scope and known limitation

MGMB persistence has been extended in format version 1.1 with a dedicated mesh-group
section. The reader remains backward-compatible with version 1.0 files: those files
load with an empty generic registry, while new files preserve group source, CAD entity
IDs, element memberships and node memberships. The section is length-bounded and
validated while reading.

TetGen and Netgen adapters are not yet connected to this registry. They should
populate the same generic contract from their own boundary/subdomain metadata;
their marker systems must not be treated as the canonical membership model.


## MGMB 1.1 persistence (Phase 3.2)

The MGMB minor version is now 1.1. The six sections are node IDs, coordinates,
element blocks, node sets, physical groups and generic mesh groups. The new section
stores each group's name, topological dimension, source kind, source CAD entity IDs,
element IDs and node indices. Values are encoded explicitly in little-endian form;
no compiler struct layout is written to disk.

The reader accepts legacy MGMB 1.0 files with five sections and initializes their
generic registry as empty. New writes always produce 1.1. Corrupt counts, invalid
source values, truncated lists, invalid node indices and invalid group references are
rejected through the normal structural validation path.
