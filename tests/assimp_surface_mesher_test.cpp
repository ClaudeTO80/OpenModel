#include <OpenModel/AssimpSurfaceMesher.hpp>
#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>
#include <OpenModel/GltfWriter.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() / "openmodel_assimp_surface_fixture.glb";
    try {
        SurfaceMesh fixture;
        SurfaceMeshPart part;
        part.name = "PlaneBoxCylinder";
        part.positions = {
            // Plane selector: z=0
            0,0,0,  1,0,0,  0,1,0,
            // Deliberately not on z=0
            0,0,1,  1,0,1,  0,1,1,
            // AABB selector: x=2
            2,0,0,  2,1,0,  2,0,1,
            // Cylinder side selector: radius=1 around the Z axis
            -1,0,0,  0,-1,0,  -1,0,1,
            // Two adjacent triangles forming one planar connected patch
            4,0,0,  4,1,0,  4,0,1,  4,1,1
        };
        part.indices = {
            0,1,2, 3,4,5, 6,7,8, 9,10,11, 12,13,14, 13,15,14
        };
        fixture.parts.push_back(part);
        const auto written = GltfWriter::writeGlbFile(path.string(), fixture);
        require(written.success, "Could not create GLB fixture: " + written.error);

        auto model = std::make_shared<AssimpModel>();
        require(model->loadFromFile(path.string()), "Assimp load failed: " + model->getLastError());

        MeshingOptions options;
        options.dimension = MeshDimension::Surface;

        AssimpSurfaceSelector plane;
        plane.name = "FIXED_END";
        plane.kind = AssimpSurfaceSelectorKind::Plane;
        plane.point = {{0,0,0}};
        plane.direction = {{0,0,1}};
        plane.tolerance = 1e-6;
        plane.constrainNormal = true;
        plane.minNormalAlignment = 0.99;

        AssimpSurfaceSelector box;
        box.name = "BOX_PATCH";
        box.kind = AssimpSurfaceSelectorKind::AxisAlignedBox;
        box.boundsMin = {{2.0, 0.0, 0.0}};
        box.boundsMax = {{2.0, 1.0, 1.0}};
        box.tolerance = 1e-6;
        box.constrainNormal = false;

        AssimpSurfaceSelector cylinder;
        cylinder.name = "CYLINDER_SIDE";
        cylinder.kind = AssimpSurfaceSelectorKind::CylinderSide;
        cylinder.point = {{0,0,0}};
        cylinder.direction = {{0,0,1}};
        cylinder.radius = 1.0;
        cylinder.axialMin = 0.0;
        cylinder.axialMax = 1.0;
        cylinder.tolerance = 1e-6;
        cylinder.constrainNormal = false;

        AssimpSurfaceSelector normal;
        normal.name = "Z_NORMAL_PATCH";
        normal.kind = AssimpSurfaceSelectorKind::Normal;
        normal.direction = {{0,0,1}};
        normal.constrainNormal = true;
        normal.minNormalAlignment = 0.99;

        AssimpSurfaceMesher mesher;
        MeshData mesh;
        require(mesher.buildMesh(model, options, {plane, normal, box, cylinder}, mesh),
                "Assimp surface meshing failed.");

        const auto* planeGroup = mesh.getMeshGroupRegistry().find("FIXED_END");
        const auto* normalGroup = mesh.getMeshGroupRegistry().find("Z_NORMAL_PATCH");
        const auto* boxGroup = mesh.getMeshGroupRegistry().find("BOX_PATCH");
        const auto* cylinderGroup = mesh.getMeshGroupRegistry().find("CYLINDER_SIDE");
        require(planeGroup && normalGroup && boxGroup && cylinderGroup,
                "One or more geometric groups are missing.");
        require(planeGroup->source == MeshGroupSource::Imported && planeGroup->dimension == 2,
                "Imported group metadata is incorrect.");
        require(planeGroup->elementIds.size() == 1, "Plane selector did not isolate one triangle.");
        require(normalGroup->elementIds.size() == 2,
                "Normal selector did not isolate both +Z-facing triangles.");
        require(boxGroup->elementIds.size() == 1, "AABB selector did not isolate one triangle.");
        require(cylinderGroup->elementIds.size() == 1, "Cylinder selector did not isolate one triangle.");
        require(planeGroup->nodeIndices.size() == 3, "Plane selector did not create a 3-node set.");

        const auto planeNodes = mesh.getNodeSets().find("FIXED_END_NODES");
        const auto normalNodes = mesh.getNodeSets().find("Z_NORMAL_PATCH_NODES");
        const auto boxNodes = mesh.getNodeSets().find("BOX_PATCH_NODES");
        const auto cylinderNodes = mesh.getNodeSets().find("CYLINDER_SIDE_NODES");
        require(planeNodes != mesh.getNodeSets().end() && planeNodes->second.size() == 3,
                "Plane node set is missing or incorrect.");
        require(normalNodes != mesh.getNodeSets().end() && normalNodes->second.size() == 6,
                "Normal node set is missing or incorrect.");
        require(boxNodes != mesh.getNodeSets().end() && boxNodes->second.size() == 3,
                "AABB node set is missing or incorrect.");
        require(cylinderNodes != mesh.getNodeSets().end() && cylinderNodes->second.size() == 3,
                "Cylinder node set is missing or incorrect.");
        require(mesh.getNumElements() == 6, "Surface import did not preserve all source triangles.");

        // Analyze connected patches independently of CAD topology.
        std::vector<AssimpSurfacePatch> patches;
        std::vector<std::string> patchDiagnostics;
        require(AssimpSurfacePatchAnalyzer::analyze(model->getRenderMeshes(), 1e-6,
                    patches, patchDiagnostics), "Connected patch analysis failed.");
        require(patches.size() == 5, "Expected four isolated triangles and one two-triangle patch.");
        const auto connected = std::find_if(patches.begin(), patches.end(),
            [](const AssimpSurfacePatch& p) { return p.triangleIndices.size() == 2; });
        require(connected != patches.end(), "Adjacent triangles were not merged into one patch.");
        require(std::abs(connected->area - 1.0) < 1e-6,
                "Connected patch area is incorrect.");
        require(connected->boundaryEdgeCount == 4,
                "Two-triangle patch should have four boundary edges.");
        require(connected->maxPlanarityDeviation < 1e-6,
                "Planar patch has an unexpected planarity deviation.");
        require(connected->kind == AssimpSurfacePatchKind::Planar,
                "Planar patch classification is incorrect.");
        require(connected->normalCoherence > 0.999999,
                "Planar patch normal coherence is incorrect.");
        require(connected->relativePlanarityDeviation < 1e-6,
                "Relative planarity deviation is incorrect.");

        // Keep the original plane-only API working for existing callers.
        AssimpPlaneSurfaceGroup legacyPlane;
        legacyPlane.name = "LEGACY_PLANE";
        legacyPlane.point = {{0,0,0}};
        legacyPlane.normal = {{0,0,1}};
        legacyPlane.tolerance = 1e-6;
        legacyPlane.minNormalAlignment = 0.99;
        MeshData legacyMesh;
        require(mesher.buildMesh(model, options, {legacyPlane}, legacyMesh),
                "Backward-compatible plane selector overload failed.");
        require(legacyMesh.getMeshGroupRegistry().find("LEGACY_PLANE") != nullptr,
                "Backward-compatible plane group was not created.");

        MeshingOptions volumeOptions;
        volumeOptions.dimension = MeshDimension::Volume;
        MeshData rejected;
        require(!mesher.buildMesh(model, volumeOptions, std::vector<AssimpSurfaceSelector>{plane}, rejected),
                "Assimp surface adapter must reject volume-mesh requests.");

        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::cout << "Assimp plane, normal, AABB and cylinder surface selection passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
