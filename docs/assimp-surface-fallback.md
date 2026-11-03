# Assimp surface import and geometric selection (without OCC)

## Scope

The Assimp backend imports existing polygonal triangles. It does not expose CAD B-Rep faces, edges, vertices, analytic surfaces, or solid topology. The selectors below therefore create mesh-derived groups and never invent CAD entity identifiers.

## Build

```bash
cmake -S . -B build-assimp \
  -DOPENMODEL_BUILD_ASSIMP_BACKEND=ON \
  -DOPENMODEL_BUILD_OCC_BACKEND=OFF \
  -DOPENMODEL_BUILD_TESTS=ON
cmake --build build-assimp --parallel
ctest --test-dir build-assimp -R 'assimp_(model|surface_mesher)_test' --output-on-failure
```

## Selector types

All coordinates and tolerances use the source model's coordinate system and units.

- **Plane**: all three vertices of a triangle must lie within the absolute plane-distance tolerance. The normal constraint compares the absolute dot product of the triangle normal and selector direction.
- **Normal**: selects triangles by normal direction alone. The absolute dot product accepts either triangle winding; use a high alignment threshold to restrict the angular deviation.
- **AxisAlignedBox**: all three vertices must lie inside the axis-aligned box, expanded by the tolerance. This is a spatial patch/region selector; use a thin box and optionally a normal constraint to isolate a particular side.
- **CylinderSide**: all three vertices must lie within the radial tolerance of the cylinder radius and between the axial limits. It selects the lateral cylindrical patch, not the end caps. Use plane selectors for caps.

A triangle is included as a whole. The implementation does not clip triangles at selector boundaries or create new nodes.

## Example

```cpp
AssimpSurfaceSelector clamp;
clamp.name = "FIXED_END";
clamp.kind = AssimpSurfaceSelectorKind::Plane;
clamp.point = {{0, 0, -500}};
clamp.direction = {{0, 0, 1}};
clamp.tolerance = 1e-4;
clamp.constrainNormal = true;
clamp.minNormalAlignment = 0.95;

AssimpSurfaceMesher mesher;
MeshData mesh;
MeshingOptions options;
options.dimension = MeshDimension::Surface;
if (!mesher.buildMesh(assimpModel, options, {clamp}, mesh))
    throw std::runtime_error("Assimp surface import failed");
```

Each matched selector creates a dimension-2 imported MeshGroup, a physical group, and a node set named `<selector>_NODES`. Imported surface triangles remain in the mesh and can belong to multiple groups.

## Limitations

- Assimp's triangulation is not a CAD B-Rep. A mesh-derived group is not a stable CAD face reference.
- No triangle clipping is performed; boundary triangles are either fully selected or fully rejected.
- The cylinder selector tests the triangle vertices. For coarse tessellations or large tolerances, refine the input mesh if selection quality is insufficient.
- The adapter imports surface triangles only. It does not create volume elements or prove that a shell is closed/manifold.
- STEP/IGES/BREP should be converted with a CAD-capable tool into a polygonal format supported by the configured Assimp build. Unit metadata may be absent or lost in conversion.


## Connected patch analysis

`AssimpSurfacePatchAnalyzer` identifies edge-connected triangle components independently
inside each imported part. It uses a configurable coordinate weld tolerance to recognize
coincident edge endpoints without changing the source mesh. Two triangles are connected only
when their winding-independent normal angle is below the configurable maximum dihedral angle
(default 45 degrees), so sharp creases split into separate patches. Each result reports:

- source part index and triangle indices;
- total triangle area and area-weighted centroid;
- area-weighted mean normal;
- axis-aligned bounds;
- maximum distance of patch vertices from the centroid/mean-normal plane;
- number of boundary edges.

This is a mesh-topology analysis, not a CAD surface reconstruction. A component can contain
curved or folded geometry; in that case the reported planarity deviation is a diagnostic,
not a proof of a planar analytic surface. The analyzer does not merge patches across
different Assimp parts and does not alter or weld the imported mesh.


## Conservative patch classification

Each analyzed patch is classified as `Planar` or `NonPlanar`. The decision uses maximum vertex distance from the area-weighted mean plane, normalized by the patch bounding-box diagonal, and area-weighted normal coherence. The threshold is relative to patch size, with a small absolute floor. This is deliberately conservative: `NonPlanar` does not mean cylindrical. It may represent a cylinder, cone, sphere, freeform surface, or a folded/inconsistently oriented mesh. The analyzer reports `normalCoherence` and both absolute and relative planarity deviations so callers can apply application-specific thresholds without mistaking a mesh heuristic for analytic surface recognition.
