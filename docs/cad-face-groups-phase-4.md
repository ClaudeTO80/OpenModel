# CAD Face to Gmsh Surface Mapping

## Scope

The Gmsh adapter maps CAD topological faces to the two-dimensional mesh entities
created by importing the same CAD file through Gmsh's OpenCASCADE kernel. The
mapping is geometric: CAD face IDs, Gmsh surface tags and mesh element IDs belong
to different identifier spaces and must never be treated as interchangeable.

The result is stored in `MeshGroupRegistry` as dimension-2 groups. Gmsh physical
groups are generated as a solver/mesh-transfer representation of the same
membership; they are not the canonical identity of a CAD face.

## Matching procedure

For each Gmsh surface entity:

1. Read its 2-D element blocks and element connectivity.
2. Select up to 32 element samples distributed over the entity.
3. Compute each sample point from the primary corner nodes. High-order midside
   nodes are deliberately excluded from the centroid calculation.
4. Project the sample onto the exact Gmsh surface with
   `gmsh::model::getClosestPoint`. This avoids testing the off-surface centroid
   of a straight-sided approximation against a curved CAD surface.
5. Query `CADModel::facesContainingPoint` at the projected point.
6. Vote for every CAD face hit by each probe. A face is retained only if it
   explains at least 80% of the probes.
7. When volume adjacency is available, retain only faces owned by one of the
   CAD solids matched to the adjacent Gmsh volumes. This disambiguates coincident
   faces belonging to different assembly components. For surface-only meshes,
   where there are no upward volume adjacencies, only geometrically unique
   component ownership is accepted.

The geometric tolerance is scale-aware and derived from the Gmsh model bounding
box diagonal. The original CAD model remains the authority for face queries.

## Surface-only meshes

CAD group mapping is now run whenever `MeshingOptions::importCadGroups` is true,
not only for volume meshes. Thus a surface-only mesh can receive face groups
without generating tetrahedra. The dimension-2 mesh elements and their nodes
are registered in the same backend-independent mesh group registry used by
volume meshing and solver writers.

## Regression coverage

`gmsh_assembly_test` exercises:
- independent volume meshing;
- conformal volume meshing;
- named CAD face and layer mapping;
- surface-only meshing with no tetrahedral elements;
- equality of the mapped `TopFace` and `Interface` element memberships.

## Limitations and next validation

The voting threshold is a conservative geometric heuristic, not a persistent
CAD-to-mesh identity. Very small faces with too few mesh elements, heavily
overlapping/coincident geometry, or invalid/healed CAD imports can remain
ambiguous and should be reported rather than guessed. The next validation
increment should use the `turbine-blade.STEP` model to measure match coverage,
unmatched surfaces and false/ambiguous candidates at both linear and quadratic
orders.

## Mapping audit API

`GmshMesher::faceMappingReport()` exposes one `CADFaceMappingRecord` per
meshed Gmsh surface whenever CAD face ownership is available through the imported
assembly structure or face groups. Each record contains:

- Gmsh surface tag and total 2-D element count;
- number of sampled elements;
- maximum per-CAD-face vote count and its ratio to sampled probes;
- CAD face IDs accepted after ownership filtering;
- an ambiguity flag for surface-only cases that cannot distinguish coincident
  CAD faces.

The report is reset for every `buildMesh` call. Diagnostics also include an
aggregate mapped/unmatched/ambiguous surface count. A mapped record must have a
sample ratio of at least 0.8; the regression tests assert this invariant and
check that each reported Gmsh surface occurs only once.

The report is an audit aid, not a claim that every mesh element has been
individually projected onto the CAD B-Rep. The current production algorithm
uses a bounded deterministic sample (up to 32 elements per surface), so a
separate high-assurance exhaustive validation mode remains a possible future
extension for small or safety-critical models.
