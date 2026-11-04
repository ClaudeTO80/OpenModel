# CAD-to-FEM group mapping — Phase 3.3

## Scope

Phase 3.3 connects the TetGen and Netgen volume-mesher adapters to the
backend-independent `MeshGroupRegistry`. CAD face membership is represented by
actual 2D boundary elements plus the nodes belonging to those elements.

## TetGen adapter

The input PLC facets inherit a positive facet marker associated with the
`RenderSubMesh::sourceFaceId`. Facets without source-face metadata receive a
separate non-CAD marker so their boundary elements can still be retained.
TetGen is invoked with `-f` to return boundary triangles and propagated markers.

The adapter then:

1. Imports tetrahedra as 3D elements.
2. Imports marked output boundary triangles as 2D elements (Triangle3 or Triangle6
   for second-order meshes, using TetGen's `o2facelist`).
3. Resolves each marker to its original CAD face ID.
4. Builds generic groups from CAD face groups and user-defined additional groups.
5. Derives node membership from the boundary triangles.

A requested group is rejected, never emitted partially, if any requested face ID has no
mapped boundary elements. The rejection is reported in `TetGenMesher::diagnostics()` and does
not abort the mesh (the same policy as the Gmsh adapter). Group names are made unique across
dimensions with a `_dim<N>` suffix instead of failing. The mapping depends on
the CAD importer retaining valid `sourceFaceId` values in its render submeshes.

## Netgen adapter

The Netgen C interface exposes surface element connectivity through
`Ng_GetNSE` and `Ng_GetSurfaceElement`, but does not provide a stable CAD face
identifier in that API. OpenModel therefore:

1. Imports Netgen volume elements.
2. Imports supported surface element types as 2D elements.
3. Queries `CADModel::facesContainingPoint` at each corner node.
4. Intersects the candidate face IDs, retaining only CAD faces common to all corners.
5. Builds generic groups and node memberships from the geometric associations.

Supported surface types are NG_TRIG, NG_QUAD, NG_TRIG6 and NG_QUAD8. NG_QUAD6
is rejected explicitly because OpenModel currently has no matching six-node
quadrilateral topology.

Requested groups are rejected if one or more requested CAD face IDs have no
mapped surface elements. This avoids silently applying solver constraints to
only part of a selected face group.

## Workbench integration

The workbench no longer requires Gmsh for user-defined CAD face groups. It reads
the canonical `MeshGroupRegistry` for all backends and derives the solver Node
Set `<group>_NODES` from the mapped node membership. Physical groups remain
available for Gmsh and existing solver export paths, but are not required for
this operation.

## Important limitations

- TetGen's face mapping requires `sourceFaceId` metadata on CAD render submeshes.
  It does not currently reconstruct face IDs from geometry when that metadata is
  absent.
- Netgen's mapping uses a geometric centroid query. Its reliability depends on
  the CAD backend's `facesContainingPoint` implementation and the selected
  tolerance.
- Edge and vertex groups are not mapped yet.
- Solid-group transfer for TetGen and Netgen is not implemented in this phase.
- Optional adapter integration tests run only when their corresponding
  third-party backend is enabled. A passing core CI job alone does not prove
  these optional integrations compile or run.
