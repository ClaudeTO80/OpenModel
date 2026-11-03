# Assimp surface-mesh fallback (no Open CASCADE)

OpenModel can import an existing polygonal surface model through Assimp without Open CASCADE. The Assimp adapter is a mesh importer, not a B-Rep CAD kernel: it does not provide reliable CAD face, edge, vertex, or solid identifiers, and it does not read STEP as a general CAD format.

## Build

Enable the existing Assimp backend:

```bash
cmake -S . -B build-assimp \
  -DOPENMODEL_BUILD_ASSIMP_BACKEND=ON \
  -DOPENMODEL_BUILD_OCC_BACKEND=OFF \
  -DOPENMODEL_BUILD_TESTS=ON
cmake --build build-assimp --parallel
ctest --test-dir build-assimp -R 'assimp_(model|surface_mesher)_test' --output-on-failure
```

Assimp must be available as a CMake package (for example through vcpkg or an installed Assimp SDK).

## Surface import and plane-based groups

`AssimpSurfaceMesher` converts the triangles already imported by `AssimpModel` into `MeshData` Triangle3 elements. It does not remesh or create tetrahedra. Use `MeshDimension::Surface`.

A `AssimpPlaneSurfaceGroup` selects triangles for which:
- all three corner vertices are within the specified absolute distance tolerance of the plane;
- the triangle normal has an absolute dot product with the normalized plane normal greater than or equal to `minNormalAlignment`.

The absolute dot product deliberately accepts either triangle winding. For a clamp at the negative Z end of a beam, for example, use point `{0,0,-500}`, normal `{0,0,1}`, and a tolerance appropriate to the model's units.

Each successful selection creates:
- a dimension-2 `MeshGroup` with source `Imported`;
- a physical surface group;
- a `<name>_NODES` node set containing the exact union of nodes used by the selected triangles.

The original imported triangles remain in the mesh; selecting a boundary does not discard or duplicate them.

## Limitations and safety

- This is a **mesh-based** selection, not a CAD-face mapping. No CAD face IDs are fabricated.
- It only selects triangles that are already coplanar within tolerance. It is not suitable as-is for curved caps or arbitrary non-planar patches.
- The adapter imports a surface mesh only. A closed triangle shell is not automatically treated as a valid solid, and no volume elements are generated.
- Assimp-supported polygonal formats (such as STL, OBJ, PLY, and glTF/GLB, depending on the Assimp build) can be used. STEP/IGES/BREP still require a CAD-capable converter/kernel.
- Units and coordinate systems are inherited from the source file; tolerances must use those same units.
