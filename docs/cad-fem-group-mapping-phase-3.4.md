# CAD-to-FEM group mapping — Phase 3.4

## Scope

Phase 3.4 extends the CAD entity contract and mesh-group mapping from faces and
solids to edges and vertices. CAD entity IDs are importer-local identifiers and
are not intended to be stable across a different import/order of the same file.

The canonical representation remains `MeshGroupRegistry`: a group has a
topological dimension (0..3), source entity IDs, optional mesh element IDs and
node indices. Node membership is many-to-many.

## CAD model contract

`CADGroup::dimension` now uses the full topological range:

- 0: vertices
- 1: edges
- 2: faces
- 3: solids

`CADModel` exposes two additional geometric queries:

- `edgesContainingPoint(point, tolerance)`
- `verticesContainingPoint(point, tolerance)`

The default implementations return an empty result, preserving compatibility
with existing CADModel subclasses.

## Open CASCADE implementation

`OCCModel` builds unique indexed maps for edges and vertices alongside the
existing solid and face maps. It also builds conservative bounding boxes for
the lower-dimensional entities.

Edge matching uses a point-to-edge B-Rep distance query. Vertex matching uses
the Euclidean distance to the native B-Rep vertex point. Both are tolerance
bounded and return importer-local IDs consistent with the corresponding maps.

XDE layer and named-subshape extraction now retains the highest-dimensional
entity represented by a named shape: solids, faces, edges/wires or vertices.
This avoids expanding a solid group into every edge and vertex of that solid.
STEP ADVANCED_FACE names continue to be handled as face groups.

## Gmsh adapter

When CAD group import is enabled, the adapter now extracts 0D and 1D mesh
entities in addition to the existing 2D and 3D entities.

- CAD edge groups are mapped to Gmsh curves by intersecting CAD edge candidates
  found at the curve's mesh nodes. Their 1D elements and nodes are registered.
- CAD vertex groups are mapped by querying all mesh nodes against the CAD
  vertex geometry. These groups are node-based; a solver point load or
  displacement constraint can use the resulting node membership.
- Edge groups are also represented as Gmsh physical groups of dimension 1.
  Vertex groups are stored in the generic registry because a 0D physical
  element group is not required to express a nodal boundary condition.

## TetGen and Netgen adapters

Both adapters transfer CAD edge and vertex memberships as node-only generic
groups using their CAD geometric queries. They do not currently export a
canonical set of 1D CAD-edge mesh elements. For edge groups, all generated
mesh nodes lying on the requested CAD edges are retained. For vertex groups,
nodes within the geometric tolerance of the requested CAD vertices are
retained.

The limitation is explicit: consumers that need line elements for beam,
spring or line-load formulations must use a backend path that supplies those
elements, currently Gmsh. The node-only groups remain suitable for nodal
constraints and point/edge support selection.

## Validation and limitations

- Missing requested CAD entity IDs are reported; adapters do not silently
  create partial lower-dimensional groups.
- The tolerance is scale-aware and based on the generated mesh bounding-box
  diagonal.
- CAD entity IDs are local to a particular imported B-Rep. A persistent
  cross-revision identity contract is a separate concern.
- Edge and vertex groups from STEP/XDE depend on the source file actually
  carrying named subshape or layer membership for those entities.
- The Netgen/TetGen edge representation is node-only; only Gmsh currently
  retains explicit 1D elements for CAD edge groups.
