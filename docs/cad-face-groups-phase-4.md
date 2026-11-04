# CAD Face to Gmsh Surface Mapping

## Scope

The Gmsh adapter maps CAD topological faces to the two-dimensional mesh entities
created by importing the same CAD file through Gmsh's OpenCASCADE kernel. The
mapping is geometric: CAD face IDs, Gmsh surface tags and mesh element IDs belong
to different identifier spaces and must never be treated as interchangeable.

The result is stored in `MeshGroupRegistry` as dimension-2 groups. Gmsh physical
groups are generated as a solver/mesh-transfer representation of the same
membership; they are not the canonical identity of a CAD face. The OCC XDE reader also imports names written directly on STEP `EDGE_CURVE`
and `VERTEX_POINT` entities, preserving named lower-dimensional CAD selections
when a writer does not serialize them as XDE sub-shape labels. Imported named
face groups and user-defined face groups are atomic: if even one requested CAD
face cannot be mapped, OpenModel does not create a partial group and reports
the missing source face IDs.

## Matching procedure

For each Gmsh surface entity:

1. Read every 2-D element block and its connectivity.
2. Construct one representative point for every element from its primary
   corner nodes. High-order midside nodes are excluded from this representative
   point so that the same procedure applies to linear and quadratic elements.
3. Project every representative point onto the exact trimmed Gmsh surface with
   `gmsh::model::getClosestPoint`. This avoids testing the off-surface centroid
   of a straight-sided approximation against a curved CAD surface.
4. Count failed projections explicitly. They remain in the denominator of the
   match ratio and cannot silently improve a candidate's score.
5. Query `CADModel::facesContainingPoint` at each successfully projected point.
6. Vote for every CAD face hit by each probe. A face is retained only if it
   explains at least 80% of all elements on the surface, including elements
   whose representative point could not be projected.
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
- quadratic Triangle6 and recombined Quadrilateral9 / incomplete Quadrilateral8
  face meshes, including exact Node Set membership for all high-order nodes;
- equality of the mapped `TopFace` and `Interface` element memberships;
- direct STEP `EDGE_CURVE` and `VERTEX_POINT` name import in the OCC structure fixture;
- atomic rejection of an intentionally incomplete user-defined face group,
  including the missing CAD face ID in diagnostics;
- Abaqus/CalculiX INP export of a surface-only CAD face group as both an
  element set and a derived node set.

`gmsh_step_face_mapping_test`, enabled by setting
`OPENMODEL_GMSH_TEST_STEP` to a real STEP file, runs the end-face mapping at
quadratic order, requires Triangle6 boundary elements, checks that all nodes
(including midside nodes) are present in the Node Set, compares the mapped
surface area against the known geometric area, and verifies that the sum of
per-surface audit element counts equals the total exported 2-D element count.
The self-contained XDE fixture also exercises second-order surface mapping
without requiring an external CAD asset. It generates recombined complete
Quadrilateral9 and incomplete Quadrilateral8 meshes and checks exact node-set
membership, while the optional STEP test remains suitable for the turbine-blade
regression model or another STEP model with known face geometry.

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
- number of per-element representative points checked and failed projections;
- maximum per-CAD-face vote count and its ratio to all surface elements;
- CAD face IDs accepted after ownership filtering;
- an ambiguity flag for surface-only cases that cannot distinguish coincident
  CAD faces.

The report is reset for every `buildMesh` call. Diagnostics also include an
aggregate mapped/unmatched/ambiguous surface count. A mapped record must have a
sample ratio of at least 0.8; the regression tests assert this invariant and
check that each reported Gmsh surface occurs only once.

The matcher now evaluates one representative point for every exported 2-D
element on each Gmsh surface; it no longer uses a bounded 32-element sample.
The audit reports the total element count, the number of per-element points
attempted, the number of failed projections, and the maximum CAD-face vote
count. The regression fixture requires exact equality between element and
sample counts and zero projection failures for every generated surface.

This is exhaustive at the element-representative-point level, not a proof that
every point of a curved/high-order element lies on the same B-Rep face. A
stronger intra-element verification using multiple quadrature points would be
appropriate for certification-grade geometric validation.

The repository does not bundle the turbine-blade STEP file: the real-model
test is therefore opt-in and cannot be considered executed by the default CI.
The new `cad-face-mapping` CI job runs the self-contained XDE assembly fixture
against Gmsh and Open CASCADE on Linux.
