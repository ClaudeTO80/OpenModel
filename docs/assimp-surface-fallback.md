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

- **Plane**: all three vertices of a triangle must lie within the absolute plane-distance tolerance. The optional normal constraint compares the absolute dot product of the triangle normal and selector direction. It is disabled by default; the `Normal` selector always applies it.
- **Normal**: selects triangles by normal direction alone. The absolute dot product accepts either triangle winding; use a high alignment threshold to restrict the angular deviation.
- **AxisAlignedBox**: all three vertices must lie inside the axis-aligned box, expanded by the tolerance. This is a spatial patch/region selector; use a thin box and optionally a normal constraint to isolate a particular side.
- **CylinderSide**: all three vertices must lie within the radial tolerance of the cylinder radius and between the axial limits. It selects the lateral cylindrical patch, not the end caps. Use plane selectors for caps.
- **SphereSurface**: all three vertices must lie within the radial tolerance of a sphere centered at `point` with the supplied `radius`.
- **ConeSide**: all three vertices must lie within tolerance of a right circular cone. `point` is the apex, `direction` points from the apex into the cone, `coneHalfAngleRadians` is measured from that axis, and the axial interval is non-negative.

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


## Patch-to-FEM group conversion

The analyzer returns ordered patch records. `AssimpPatchGroupBuilder` converts one or more of those records into FEM groups without modifying the imported mesh:

```cpp
AssimpSurfaceMesher mesher;
MeshingOptions options;
options.dimension = MeshDimension::Surface;
MeshData mesh;
if (!mesher.buildMesh(assimpModel, options, mesh))
    throw std::runtime_error("Surface import failed");

std::vector<AssimpSurfacePatch> patches;
std::vector<std::string> diagnostics;
if (!AssimpSurfacePatchAnalyzer::analyze(
        assimpModel->getRenderMeshes(), 1e-6, patches, diagnostics))
    throw std::runtime_error("Patch analysis failed");

AssimpPatchGroupDefinition fixed;
fixed.name = "FIXED_END";
fixed.patchIndices = {0, 1}; // indices into the analyzer result
fixed.materialName = "STEEL";

if (!AssimpPatchGroupBuilder::apply(
        assimpModel->getRenderMeshes(), patches, mesher.sourceTriangleElementIds(),
        {fixed}, mesh, diagnostics))
    throw std::runtime_error("Patch group conversion failed");
```

The builder creates a dimension-2 imported `MeshGroup`, a physical group and a `<name>_NODES` node set. A definition can combine multiple connected patches. Element memberships use the exact source-triangle-to-element map recorded by AssimpSurfaceMesher, including zero entries for skipped degenerate triangles; the builder does not reconstruct or guess this mapping. Invalid references are rejected before the mesh is modified. Groups are mesh-derived and do not carry CAD face identifiers.

The patch analyzer uses a spatial hash with neighbouring-cell distance checks for welding, rather than rounded coordinate buckets. This avoids missing coincident endpoints merely because they fall on opposite quantization-cell boundaries. Normal statistics are winding-independent. It also reports non-manifold edges (edges shared by more than two triangles) separately from ordinary patch boundary edges.


### Automatic patch filtering

A group definition can use `selectByFilter=true` instead of explicit patch indices. The filter can constrain planar classification, area interval, maximum absolute planarity deviation, minimum normal coherence, centroid bounds and mean-normal alignment. Direction vectors are normalized internally; normal alignment is winding-independent. A filter that matches no patches is treated as an error so an intended boundary condition cannot silently become empty.

```cpp
AssimpPatchGroupDefinition endFaces;
endFaces.name = "END_FACES";
endFaces.selectByFilter = true;
endFaces.filter.planarOnly = true;
endFaces.filter.constrainNormal = true;
endFaces.filter.direction = {{0, 0, 1}};
endFaces.filter.minNormalAlignment = 0.98;
endFaces.filter.minArea = 10.0;
AssimpPatchGroupBuilder::apply(parts, patches, {endFaces}, mesh, diagnostics);
```


## Installed CMake package

When the Assimp backend is enabled, the build exports and installs `OpenModel::Assimp` alongside `OpenModel::Core`. The installed package resolves Assimp as a dependency. Consumers can request the component explicitly:

```cmake
find_package(OpenModel CONFIG REQUIRED COMPONENTS Assimp)
target_link_libraries(my_app PRIVATE OpenModel::Assimp)
```

The repository example `examples/cmake-assimp-consumer` is built in the Assimp CI job after installing the package, so the exported target and transitive Assimp dependency are exercised separately from the in-tree build.


## Analytic surface fitting from patches

`AssimpSurfaceGeometryFitter` estimates simple geometric primitives from each patch's unique mesh vertices. It supports planar, cylindrical, spherical and conical fits; patches that do not meet the residual and sampling criteria remain `Freeform`. Results include fitted center/axis, radius, cone half-angle, sample count, angular coverage and absolute/relative RMS and maximum residuals.

```cpp
AssimpSurfaceFitOptions fitOptions;
fitOptions.maxRelativeRmsError = 1e-3;
fitOptions.maxRelativeMaxError = 5e-3;
fitOptions.minAngularCoverageDegrees = 45.0;

std::vector<AssimpSurfaceFit> fits;
if (!AssimpSurfaceGeometryFitter::fit(
        assimpModel->getRenderMeshes(), patches, fitOptions, fits, diagnostics))
    throw std::runtime_error("Surface fitting failed");
```

The fitter tries PCA-derived candidate axes, algebraic circle/sphere least-squares fits and a linear radius-versus-axial-coordinate cone fit. A minimum of eight unique vertices is required for non-planar primitive fitting by default to reduce overfitting of tiny patches. Residual thresholds and angular coverage are explicit acceptance criteria. This remains a mesh approximation: it does not recover exact CAD analytic surfaces, trim curves, parameterizations or B-Rep face IDs. Use it to classify/select mesh patches, not as a substitute for OpenCASCADE.
