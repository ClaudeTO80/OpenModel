#include <OpenModel/AssimpSurfaceMesher.hpp>
#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>
#include <OpenModel/AssimpPatchGroupBuilder.hpp>
#include <OpenModel/AssimpSurfaceGeometryFitter.hpp>
#include <OpenModel/AbaqusInpWriter.hpp>
#include <OpenModel/GltfWriter.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

RenderSubMesh makeCylinder(double radius, double height, unsigned int segments) {
    RenderSubMesh part;
    part.name = "Cylinder";
    for (unsigned int ring=0; ring<3; ++ring)
        for (unsigned int i=0; i<segments; ++i) {
            const double angle=2.0*3.14159265358979323846*i/segments;
            part.vertices.insert(part.vertices.end(), {
                static_cast<float>(radius*std::cos(angle)),
                static_cast<float>(radius*std::sin(angle)),
                static_cast<float>(ring*height/2.0)});
        }
    for (unsigned int band=0; band<2; ++band)
        for (unsigned int i=0; i<segments; ++i) {
            const unsigned int j=(i+1)%segments;
            const unsigned int bi=band*segments+i, bj=band*segments+j;
            const unsigned int ti=(band+1)*segments+i, tj=(band+1)*segments+j;
            part.indices.insert(part.indices.end(), {bi,bj,tj, bi,tj,ti});
        }
    return part;
}

RenderSubMesh makeCone(double bottomRadius, double topRadius, double height,
                       unsigned int segments) {
    RenderSubMesh part;
    part.name = "ConeFrustum";
    for (unsigned int ring=0; ring<3; ++ring) {
        const double fraction=ring/2.0;
        const double radius=bottomRadius+(topRadius-bottomRadius)*fraction;
        for (unsigned int i=0; i<segments; ++i) {
            const double angle=2.0*3.14159265358979323846*i/segments;
            part.vertices.insert(part.vertices.end(), {
                static_cast<float>(radius*std::cos(angle)),
                static_cast<float>(radius*std::sin(angle)),
                static_cast<float>(ring*height/2.0)});
        }
    }
    for (unsigned int band=0; band<2; ++band)
        for (unsigned int i=0; i<segments; ++i) {
            const unsigned int j=(i+1)%segments;
            const unsigned int bi=band*segments+i, bj=band*segments+j;
            const unsigned int ti=(band+1)*segments+i, tj=(band+1)*segments+j;
            part.indices.insert(part.indices.end(), {bi,bj,tj, bi,tj,ti});
        }
    return part;
}

RenderSubMesh makeSphere(double radius, unsigned int latitudeSegments,
                         unsigned int longitudeSegments) {
    RenderSubMesh part;
    part.name = "Sphere";
    const unsigned int top=0;
    part.vertices.insert(part.vertices.end(), {0.0f,0.0f,static_cast<float>(radius)});
    std::vector<std::vector<unsigned int>> rings;
    for (unsigned int lat=1; lat<latitudeSegments; ++lat) {
        const double polar=3.14159265358979323846*lat/latitudeSegments;
        std::vector<unsigned int> ring;
        for (unsigned int lon=0; lon<longitudeSegments; ++lon) {
            const double azimuth=2.0*3.14159265358979323846*lon/longitudeSegments;
            ring.push_back(static_cast<unsigned int>(part.vertices.size()/3));
            part.vertices.insert(part.vertices.end(), {
                static_cast<float>(radius*std::sin(polar)*std::cos(azimuth)),
                static_cast<float>(radius*std::sin(polar)*std::sin(azimuth)),
                static_cast<float>(radius*std::cos(polar))});
        }
        rings.push_back(std::move(ring));
    }
    const unsigned int bottom=static_cast<unsigned int>(part.vertices.size()/3);
    part.vertices.insert(part.vertices.end(), {0.0f,0.0f,static_cast<float>(-radius)});
    for (unsigned int lon=0; lon<longitudeSegments; ++lon) {
        const unsigned int next=(lon+1)%longitudeSegments;
        part.indices.insert(part.indices.end(), {top,rings.front()[lon],rings.front()[next]});
        for (std::size_t lat=0; lat+1<rings.size(); ++lat) {
            const unsigned int a=rings[lat][lon], b=rings[lat][next];
            const unsigned int c=rings[lat+1][next], d=rings[lat+1][lon];
            part.indices.insert(part.indices.end(), {a,b,c, a,c,d});
        }
        part.indices.insert(part.indices.end(), {bottom,rings.back()[next],rings.back()[lon]});
    }
    return part;
}

SurfaceMesh asSurfaceMesh(const RenderSubMesh& source) {
    SurfaceMesh mesh;
    SurfaceMeshPart part;
    part.name = source.name;
    part.positions = source.vertices;
    part.indices = source.indices;
    mesh.parts.push_back(std::move(part));
    return mesh;
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
        box.direction = {{0,0,0}}; // AABB selection does not require an axis.

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
        require(mesher.sourceTriangleElementIds().size() == model->getRenderMeshes().size(),
                "Assimp source-triangle map does not contain every imported part.");
        std::size_t mappedElements = 0;
        for (const auto& partMap : mesher.sourceTriangleElementIds())
            mappedElements += static_cast<std::size_t>(std::count_if(partMap.begin(), partMap.end(),
                [](ElementId id) { return id != 0; }));
        require(mappedElements == mesh.getNumElements(),
                "Assimp source-triangle map does not cover imported FEM elements exactly.");

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

        RenderSubMesh folded;
        folded.vertices = {0,0,0, 1,0,0, 0,1,0, 0,0,1};
        folded.indices = {0,1,2, 1,0,3};
        std::vector<AssimpSurfacePatch> foldedPatches;
        std::vector<std::string> foldedDiagnostics;
        require(AssimpSurfacePatchAnalyzer::analyze({folded}, 1e-6,
                    foldedPatches, foldedDiagnostics, 90.0),
                "Folded patch analysis failed.");
        require(foldedPatches.size() == 1 && foldedPatches.front().triangleIndices.size() == 2,
                "Folded adjacent triangles were not connected at a 90-degree threshold.");
        require(foldedPatches.front().kind == AssimpSurfacePatchKind::NonPlanar,
                "Folded patch was incorrectly classified as planar.");
        require(foldedPatches.front().normalCoherence < 0.9,
                "Folded patch normal coherence did not detect the fold.");

        RenderSubMesh nonManifoldPart;
        nonManifoldPart.vertices = {
            0,0,0, 1,0,0, 0,1,0, 0,-1,0, 0,0.1f,1
        };
        nonManifoldPart.indices = {0,1,2, 1,0,3, 0,1,4};
        std::vector<AssimpSurfacePatch> nonManifoldPatches;
        std::vector<std::string> nonManifoldDiagnostics;
        require(AssimpSurfacePatchAnalyzer::analyze({nonManifoldPart}, 1e-6,
                    nonManifoldPatches, nonManifoldDiagnostics, 45.0),
                "Non-manifold patch analysis failed.");
        require(nonManifoldPatches.size() == 2,
                "Dihedral filtering did not split the non-manifold incident triangles.");
        for (const auto& patch : nonManifoldPatches)
            require(patch.nonManifoldEdgeCount == 1,
                    "A patch touching a globally non-manifold edge did not report it.");

        // Welding must inspect neighbouring spatial-hash cells and must not
        // depend on source triangle winding.
        RenderSubMesh nearWeld;
        nearWeld.vertices = {
            0.99e-6,0,0, 0.99e-6,1,0, 1,0,0,
            1.01e-6,0,0, 1.01e-6,1,0
        };
        nearWeld.indices = {0,1,2, 3,2,4};
        std::vector<AssimpSurfacePatch> weldedPatches;
        std::vector<std::string> weldedDiagnostics;
        require(AssimpSurfacePatchAnalyzer::analyze({nearWeld}, 1e-6,
                    weldedPatches, weldedDiagnostics, 45.0),
                "Tolerance-aware welding analysis failed.");
        require(weldedPatches.size() == 1 &&
                weldedPatches.front().triangleIndices.size() == 2,
                "Vertices within weld tolerance across cell boundaries were not connected.");
        require(weldedPatches.front().kind == AssimpSurfacePatchKind::Planar,
                "Inconsistent source winding incorrectly changed planar classification.");
        require(weldedPatches.front().normalCoherence > 0.999,
                "Winding-independent normal coherence is incorrect.");

        // Fit analytic surfaces from tessellated, mesh-only patches.
        AssimpSurfaceFitOptions fitOptions;
        std::vector<AssimpSurfaceFit> fits;
        std::vector<std::string> fitDiagnostics;
        require(AssimpSurfaceGeometryFitter::fit({folded}, foldedPatches,
                    fitOptions, fits, fitDiagnostics),
                "Small-patch geometry fitting failed.");
        require(fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Freeform,
                "A four-point folded patch was overfitted to an analytic primitive.");

        // A fitted planar selector is scoped to the source patch bounds; a
        // second disconnected triangle on the same plane must not be captured.
        RenderSubMesh planeOne;
        planeOne.name = "PlaneOne";
        planeOne.vertices = {0,0,0, 1,0,0, 0,1,0};
        planeOne.indices = {0,1,2};
        RenderSubMesh planeTwo;
        planeTwo.name = "PlaneTwo";
        planeTwo.vertices = {10,0,0, 11,0,0, 10,1,0};
        planeTwo.indices = {0,1,2};
        std::vector<AssimpSurfacePatch> planePatches;
        require(AssimpSurfacePatchAnalyzer::analyze({planeOne,planeTwo}, 1e-6,
                    planePatches, fitDiagnostics) && planePatches.size() == 2,
                "Disconnected planar patch analysis failed.");
        require(AssimpSurfaceGeometryFitter::fit({planeOne,planeTwo}, planePatches,
                    fitOptions, fits, fitDiagnostics),
                "Planar patch fitting failed.");
        AssimpSurfaceSelector scopedPlaneSelector;
        std::string scopedSelectorDiagnostic;
        require(AssimpSurfaceGeometryFitter::makeSelector(fits.front(), {planeOne,planeTwo},
                    planePatches.front(), "SCOPED_PLANE", 1e-5,
                    scopedPlaneSelector, scopedSelectorDiagnostic),
                "Could not create a patch-scoped planar selector: " + scopedSelectorDiagnostic);
        require(scopedPlaneSelector.constrainBounds,
                "Fitted selector did not enable patch bounding-box scope.");
        SurfaceMesh planeScene;
        SurfaceMeshPart planeScenePart1;
        planeScenePart1.name = planeOne.name;
        planeScenePart1.positions = planeOne.vertices;
        planeScenePart1.indices = planeOne.indices;
        SurfaceMeshPart planeScenePart2;
        planeScenePart2.name = planeTwo.name;
        planeScenePart2.positions = planeTwo.vertices;
        planeScenePart2.indices = planeTwo.indices;
        planeScene.parts = {planeScenePart1,planeScenePart2};
        const auto planeScenePath = std::filesystem::temp_directory_path() / "openmodel_assimp_scoped_plane.glb";
        const auto planeSceneWrite = GltfWriter::writeGlbFile(planeScenePath.string(),planeScene);
        require(planeSceneWrite.success,"Could not write scoped plane fixture: "+planeSceneWrite.error);
        auto planeSceneModel = std::make_shared<AssimpModel>();
        require(planeSceneModel->loadFromFile(planeScenePath.string()),
                "Could not import scoped plane fixture: "+planeSceneModel->getLastError());
        MeshingOptions scopedPlaneOptions;
        scopedPlaneOptions.dimension=MeshDimension::Surface;
        AssimpSurfaceMesher scopedPlaneMesher;
        MeshData scopedPlaneMesh;
        require(scopedPlaneMesher.buildMesh(planeSceneModel,scopedPlaneOptions,
                    {scopedPlaneSelector},scopedPlaneMesh),
                "Patch-scoped plane selector failed.");
        const auto* scopedPlaneGroup=scopedPlaneMesh.getMeshGroupRegistry().find("SCOPED_PLANE");
        require(scopedPlaneGroup && scopedPlaneGroup->elementIds.size()==1 &&
                scopedPlaneMesh.getNumElements()==2,
                "Patch-scoped plane selector captured a disconnected coplanar patch.");
        std::error_code scopedPlaneError;
        std::filesystem::remove(planeScenePath,scopedPlaneError);
        const auto cylinderPart = makeCylinder(2.0, 3.0, 24);
        std::vector<AssimpSurfacePatch> cylinderPatches;
        require(AssimpSurfacePatchAnalyzer::analyze({cylinderPart}, 1e-6,
                    cylinderPatches, fitDiagnostics, 30.0) && cylinderPatches.size() == 1,
                "Cylinder fixture patch analysis failed.");
        require(AssimpSurfaceGeometryFitter::fit({cylinderPart}, cylinderPatches,
                    fitOptions, fits, fitDiagnostics),
                "Cylinder geometry fitting failed.");
        require(fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Cylindrical,
                "Analytic cylinder was not recognized.");
        require(std::abs(fits.front().radius - 2.0) < 1e-3,
                "Fitted cylinder radius is inaccurate.");

        RenderSubMesh tinyPlanePart;
        tinyPlanePart.vertices = {0.0f,0.0f,0.0f, 1.0e-10f,0.0f,0.0f,
                                  0.0f,1.0e-10f,0.0f};
        tinyPlanePart.indices = {0,1,2};
        std::vector<AssimpSurfacePatch> tinyPlanePatches;
        require(AssimpSurfacePatchAnalyzer::analyze({tinyPlanePart}, 1.0e-15,
                    tinyPlanePatches, fitDiagnostics) &&
                tinyPlanePatches.size() == 1 &&
                tinyPlanePatches.front().kind == AssimpSurfacePatchKind::Planar &&
                tinyPlanePatches.front().normalCoherence > 0.999999 &&
                tinyPlanePatches.front().meanNormal[2] > 0.999999,
                "Small planar geometry was not classified using scale-relative normal statistics.");

        // The geometry algorithms must not contain fixed model-unit cutoffs.
        // A small, arbitrarily oriented cylinder exercises PCA, degeneracy
        // detection, and cone/cylinder axial-extent tests at micron-like scale.
        auto tinyCylinderPart = makeCylinder(2.0e-10, 3.0e-10, 24);
        const double rotation = 0.37;
        for (std::size_t vi=0; vi<tinyCylinderPart.vertices.size()/3; ++vi) {
            const double x=tinyCylinderPart.vertices[3*vi];
            const double z=tinyCylinderPart.vertices[3*vi+2];
            tinyCylinderPart.vertices[3*vi]=static_cast<float>(
                std::cos(rotation)*x + std::sin(rotation)*z);
            tinyCylinderPart.vertices[3*vi+2]=static_cast<float>(
                -std::sin(rotation)*x + std::cos(rotation)*z);
        }
        std::vector<AssimpSurfacePatch> tinyCylinderPatches;
        require(AssimpSurfacePatchAnalyzer::analyze({tinyCylinderPart}, 1.0e-15,
                    tinyCylinderPatches, fitDiagnostics, 30.0) &&
                tinyCylinderPatches.size() == 1,
                "Scale-relative patch analysis discarded a valid small cylinder.");
        require(AssimpSurfaceGeometryFitter::fit({tinyCylinderPart}, tinyCylinderPatches,
                    fitOptions, fits, fitDiagnostics) &&
                fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Cylindrical,
                "Scale-relative analytic fitting failed on a small rotated cylinder.");
        require(std::abs(fits.front().radius / 2.0e-10 - 1.0) < 2.0e-3,
                "Small-cylinder fitted radius is inaccurate.");

        auto tinySpherePart = makeSphere(1.75e-10, 12, 32);
        std::vector<AssimpSurfacePatch> tinySpherePatches;
        require(AssimpSurfacePatchAnalyzer::analyze({tinySpherePart}, 1.0e-15,
                    tinySpherePatches, fitDiagnostics, 60.0) &&
                tinySpherePatches.size() == 1 &&
                AssimpSurfaceGeometryFitter::fit({tinySpherePart}, tinySpherePatches,
                    fitOptions, fits, fitDiagnostics) &&
                fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Spherical,
                "Scale-relative fitting failed on a small sphere.");
        require(std::abs(fits.front().radius / 1.75e-10 - 1.0) < 2.0e-3,
                "Small-sphere fitted radius is inaccurate.");

        auto tinyConePart = makeCone(2.0e-10, 0.75e-10, 3.0e-10, 24);
        for (std::size_t vi=0; vi<tinyConePart.vertices.size()/3; ++vi) {
            const double x=tinyConePart.vertices[3*vi];
            const double z=tinyConePart.vertices[3*vi+2];
            tinyConePart.vertices[3*vi]=static_cast<float>(
                std::cos(rotation)*x + std::sin(rotation)*z);
            tinyConePart.vertices[3*vi+2]=static_cast<float>(
                -std::sin(rotation)*x + std::cos(rotation)*z);
        }
        std::vector<AssimpSurfacePatch> tinyConePatches;
        require(AssimpSurfacePatchAnalyzer::analyze({tinyConePart}, 1.0e-15,
                    tinyConePatches, fitDiagnostics, 30.0) &&
                tinyConePatches.size() == 1 &&
                AssimpSurfaceGeometryFitter::fit({tinyConePart}, tinyConePatches,
                    fitOptions, fits, fitDiagnostics) &&
                fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Conical,
                "Scale-relative fitting failed on a small rotated cone.");
        require(std::abs(fits.front().coneHalfAngleRadians -
                    std::atan(std::abs(-1.25/3.0))) < 2.0e-3,
                "Small-cone fitted half-angle is inaccurate.");

        const auto tinyCylinderPath = std::filesystem::temp_directory_path() /
            "openmodel_assimp_tiny_cylinder.glb";
        const auto tinyCylinderGlb = GltfWriter::writeGlbFile(
            tinyCylinderPath.string(), asSurfaceMesh(tinyCylinderPart));
        require(tinyCylinderGlb.success,
                "Could not write tiny-cylinder selector fixture: " + tinyCylinderGlb.error);
        auto tinyCylinderModel = std::make_shared<AssimpModel>();
        require(tinyCylinderModel->loadFromFile(tinyCylinderPath.string()),
                "Could not import tiny-cylinder fixture: " + tinyCylinderModel->getLastError());
        MeshData tinyCylinderMesh;
        AssimpSurfaceMesher tinyCylinderMesher;
        require(tinyCylinderMesher.buildMesh(tinyCylinderModel, selectorOptions, tinyCylinderMesh) &&
                tinyCylinderMesh.getNumElements() == tinyCylinderPart.indices.size()/3,
                "Scale-relative Assimp meshing discarded valid small triangles.");
        std::error_code tinyCylinderError;
        std::filesystem::remove(tinyCylinderPath, tinyCylinderError);

        AssimpSurfaceSelector fittedCylinderSelector;
        std::string cylinderSelectorDiagnostic;
        require(AssimpSurfaceGeometryFitter::makeSelector(fits.front(), {cylinderPart},
                    cylinderPatches.front(), "FITTED_CYLINDER", 1e-5,
                    fittedCylinderSelector, cylinderSelectorDiagnostic),
                "Could not convert cylinder fit to a selector: " + cylinderSelectorDiagnostic);
        require(fittedCylinderSelector.kind == AssimpSurfaceSelectorKind::CylinderSide &&
                std::abs(fittedCylinderSelector.radius - 2.0) < 1e-3,
                "Fitted cylinder selector has incorrect parameters.");

        const auto spherePart = makeSphere(1.75, 8, 24);
        std::vector<AssimpSurfacePatch> spherePatches;
        require(AssimpSurfacePatchAnalyzer::analyze({spherePart}, 1e-6,
                    spherePatches, fitDiagnostics, 45.0) && spherePatches.size() == 1,
                "Sphere fixture patch analysis failed.");
        require(AssimpSurfaceGeometryFitter::fit({spherePart}, spherePatches,
                    fitOptions, fits, fitDiagnostics),
                "Sphere geometry fitting failed.");
        require(fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Spherical,
                "Analytic sphere was not recognized.");
        require(std::abs(fits.front().radius - 1.75) < 1e-3,
                "Fitted sphere radius is inaccurate.");
        require(fits.front().angularCoverageDegrees >= fitOptions.minAngularCoverageDegrees,
                "Spherical fit did not report measured directional coverage.");
        const AssimpSurfaceFit sphereFit = fits.front();

        // A small spherical cap must not inherit an artificial 360-degree
        // coverage score and be accepted as a fully supported sphere.
        const auto denseSpherePart = makeSphere(1.75, 24, 48);
        RenderSubMesh sphereCap;
        sphereCap.name = "SphereCap";
        sphereCap.vertices = denseSpherePart.vertices;
        for (std::size_t ti=0; ti<denseSpherePart.indices.size()/3; ++ti) {
            bool inside = true;
            for (std::size_t k=0; k<3; ++k) {
                const auto vi = static_cast<std::size_t>(denseSpherePart.indices[3*ti+k]);
                if (denseSpherePart.vertices[3*vi+2] / 1.75f <= 0.96f) inside = false;
            }
            if (inside)
                sphereCap.indices.insert(sphereCap.indices.end(),
                    denseSpherePart.indices.begin()+static_cast<std::ptrdiff_t>(3*ti),
                    denseSpherePart.indices.begin()+static_cast<std::ptrdiff_t>(3*ti+3));
        }
        std::vector<AssimpSurfacePatch> sphereCapPatches;
        require(AssimpSurfacePatchAnalyzer::analyze({sphereCap}, 1e-6,
                    sphereCapPatches, fitDiagnostics, 30.0) && sphereCapPatches.size() == 1,
                "Spherical-cap patch analysis failed.");
        AssimpSurfaceFitOptions capFitOptions = fitOptions;
        capFitOptions.minAngularCoverageDegrees = 100.0;
        require(AssimpSurfaceGeometryFitter::fit({sphereCap}, sphereCapPatches,
                    capFitOptions, fits, fitDiagnostics),
                "Spherical-cap geometry fitting failed.");
        require(fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Freeform,
                "A narrow spherical cap was incorrectly accepted as a complete spherical primitive.");

        const auto conePart = makeCone(2.0, 0.75, 3.0, 24);
        std::vector<AssimpSurfacePatch> conePatches;
        require(AssimpSurfacePatchAnalyzer::analyze({conePart}, 1e-6,
                    conePatches, fitDiagnostics, 30.0) && conePatches.size() == 1,
                "Cone fixture patch analysis failed.");
        require(AssimpSurfaceGeometryFitter::fit({conePart}, conePatches,
                    fitOptions, fits, fitDiagnostics),
                "Cone geometry fitting failed.");
        require(fits.size() == 1 && fits.front().type == AssimpFittedSurfaceType::Conical,
                "Analytic cone frustum was not recognized.");
        require(std::abs(fits.front().apex[0]) < 1e-3 &&
                std::abs(fits.front().apex[1]) < 1e-3 &&
                std::abs(fits.front().apex[2] - 4.8) < 1e-2 &&
                fits.front().coneSlope < 0.0,
                "Fitted cone apex or signed slope is incorrect.");

        // Re-import analytic fixtures and exercise the sphere/cone selectors
        // through the same Assimp -> MeshData path used by real models.
        const auto spherePath = std::filesystem::temp_directory_path() / "openmodel_assimp_sphere_selector.glb";
        const auto sphereGlb = GltfWriter::writeGlbFile(spherePath.string(), asSurfaceMesh(spherePart));
        require(sphereGlb.success, "Could not write sphere selector fixture: " + sphereGlb.error);
        auto sphereModel = std::make_shared<AssimpModel>();
        require(sphereModel->loadFromFile(spherePath.string()),
                "Could not import sphere selector fixture: " + sphereModel->getLastError());
        AssimpSurfaceSelector sphereSelector;
        std::string selectorDiagnostic;
        require(AssimpSurfaceGeometryFitter::makeSelector(sphereFit, {spherePart},
                    spherePatches.front(), "SPHERE_SURFACE", 1e-5,
                    sphereSelector, selectorDiagnostic),
                "Could not convert sphere fit to a selector: " + selectorDiagnostic);
        MeshingOptions selectorOptions;
        selectorOptions.dimension = MeshDimension::Surface;
        AssimpSurfaceMesher selectorMesher;
        MeshData sphereSelection;
        require(selectorMesher.buildMesh(sphereModel, selectorOptions, {sphereSelector}, sphereSelection),
                "Sphere surface selector failed.");
        const auto* sphereGroup = sphereSelection.getMeshGroupRegistry().find("SPHERE_SURFACE");
        require(sphereGroup && sphereGroup->elementIds.size() == sphereSelection.getNumElements(),
                "Sphere selector did not select the complete tessellated sphere.");

        const auto conePath = std::filesystem::temp_directory_path() / "openmodel_assimp_cone_selector.glb";
        const auto coneGlb = GltfWriter::writeGlbFile(conePath.string(), asSurfaceMesh(conePart));
        require(coneGlb.success, "Could not write cone selector fixture: " + coneGlb.error);
        auto coneModel = std::make_shared<AssimpModel>();
        require(coneModel->loadFromFile(conePath.string()),
                "Could not import cone selector fixture: " + coneModel->getLastError());
        AssimpSurfaceSelector coneSelector;
        require(AssimpSurfaceGeometryFitter::makeSelector(fits.front(), {conePart},
                    conePatches.front(), "CONE_SURFACE", 1e-5,
                    coneSelector, selectorDiagnostic),
                "Could not convert cone fit to a selector: " + selectorDiagnostic);
        require(coneSelector.kind == AssimpSurfaceSelectorKind::ConeSide &&
                coneSelector.direction[2] < -0.99 &&
                std::abs(coneSelector.point[2] - 4.8) < 1e-2,
                "Fitted cone selector does not preserve the apex orientation.");
        MeshData coneSelection;
        require(selectorMesher.buildMesh(coneModel, selectorOptions, {coneSelector}, coneSelection),
                "Cone surface selector failed.");
        const auto* coneGroup = coneSelection.getMeshGroupRegistry().find("CONE_SURFACE");
        require(coneGroup && coneGroup->elementIds.size() == coneSelection.getNumElements(),
                "Cone selector did not select the complete tessellated cone.");

        std::error_code fixtureError;
        std::filesystem::remove(spherePath, fixtureError);
        std::filesystem::remove(conePath, fixtureError);


        const auto connectedPatchIndex = static_cast<std::size_t>(
            std::distance(patches.begin(), connected));
        std::vector<AssimpPatchGroupDefinition> patchGroups = {
            {"CONNECTED_PATCH", {connectedPatchIndex}, "STEEL"},
            {"COMBINED_PATCHES", {0, 1}, ""}
        };
        AssimpPatchGroupDefinition filteredPatchGroup;
        filteredPatchGroup.name = "PLANAR_AREA_FILTER";
        filteredPatchGroup.selectByFilter = true;
        filteredPatchGroup.filter.planarOnly = true;
        filteredPatchGroup.filter.minArea = 0.99;
        filteredPatchGroup.filter.maxArea = 1.01;
        patchGroups.push_back(filteredPatchGroup);
        std::vector<std::string> groupDiagnostics;
        require(AssimpPatchGroupBuilder::apply(model->getRenderMeshes(), patches,
                    mesher.sourceTriangleElementIds(), patchGroups, mesh, groupDiagnostics),
                "Assimp patch-to-FEM group conversion failed.");
        const auto* connectedGroup = mesh.getMeshGroupRegistry().find("CONNECTED_PATCH");
        require(connectedGroup && connectedGroup->elementIds.size() == 2 &&
                connectedGroup->nodeIndices.size() == 4,
                "Connected patch FEM group has incorrect memberships.");
        require(mesh.getNodeSets().count("CONNECTED_PATCH_NODES") == 1 &&
                mesh.getNodeSets().at("CONNECTED_PATCH_NODES").size() == 4,
                "Connected patch node set was not created.");
        const auto physical = std::find_if(mesh.getPhysicalGroups().begin(),
            mesh.getPhysicalGroups().end(), [](const MeshPhysicalGroup& group) {
                return group.name == "CONNECTED_PATCH";
            });
        require(physical != mesh.getPhysicalGroups().end() && physical->materialName == "STEEL",
                "Patch physical group metadata was not preserved.");
        const auto* combinedGroup = mesh.getMeshGroupRegistry().find("COMBINED_PATCHES");
        require(combinedGroup && combinedGroup->elementIds.size() == 2,
                "Combining multiple analyzed patches into one FEM group failed.");
        const auto* filteredGroup = mesh.getMeshGroupRegistry().find("PLANAR_AREA_FILTER");
        require(filteredGroup && filteredGroup->elementIds.size() == 2,
                "Geometry-filtered patch selection did not isolate the area-one planar patch.");

        const auto solverPath = std::filesystem::temp_directory_path() / "openmodel_assimp_groups.inp";
        const auto solverExport = AbaqusInpWriter::writeFile(solverPath.string(), mesh);
        require(solverExport.success,
                "Assimp-generated FEM groups could not be exported to Abaqus/CalculiX: " +
                (solverExport.errors.empty() ? std::string{} : solverExport.errors.front()));
        {
            std::ifstream solverInput(solverPath, std::ios::binary);
            const std::string solverText((std::istreambuf_iterator<char>(solverInput)),
                                         std::istreambuf_iterator<char>());
            require(solverText.find("*ELSET, ELSET=CONNECTED_PATCH") != std::string::npos,
                    "Assimp patch element set is missing from Abaqus/CalculiX export.");
            require(solverText.find("*NSET, NSET=CONNECTED_PATCH_NODES") != std::string::npos,
                    "Assimp patch node set is missing from Abaqus/CalculiX export.");
            require(solverText.find("MATERIAL=STEEL") != std::string::npos,
                    "Assimp patch material metadata is missing from solver export.");
        }
        std::error_code solverError;
        std::filesystem::remove(solverPath, solverError);

        std::vector<std::string> invalidGroupDiagnostics;
        require(!AssimpPatchGroupBuilder::apply(model->getRenderMeshes(), patches,
                    mesher.sourceTriangleElementIds(),
                    {{"INVALID_PATCH", {patches.size()}, ""}}, mesh, invalidGroupDiagnostics),
                "Invalid patch references must be rejected.");
        require(mesh.getMeshGroupRegistry().find("INVALID_PATCH") == nullptr,
                "Invalid patch conversion partially modified the mesh.");

        // A stale/duplicated source map must not attach two source triangles
        // to the same FEM element or partially add groups.
        auto duplicateSourceMap = mesher.sourceTriangleElementIds();
        ElementId firstMappedId = 0;
        bool duplicated = false;
        for (auto& partMap : duplicateSourceMap) {
            for (auto& id : partMap) {
                if (id == 0) continue;
                if (firstMappedId == 0) firstMappedId = id;
                else { id = firstMappedId; duplicated = true; break; }
            }
            if (duplicated) break;
        }
        require(duplicated, "Could not construct duplicate source-map fixture.");
        MeshData invalidMappingMesh = mesh;
        std::vector<std::string> mappingDiagnostics;
        require(!AssimpPatchGroupBuilder::apply(model->getRenderMeshes(), patches,
                    duplicateSourceMap, {{"DUPLICATE_SOURCE_MAP", {connectedPatchIndex}, ""}},
                    invalidMappingMesh, mappingDiagnostics),
                "Duplicate source element mapping must be rejected.");
        require(invalidMappingMesh.getMeshGroupRegistry().find("DUPLICATE_SOURCE_MAP") == nullptr,
                "Invalid source mapping partially modified the mesh.");

        auto incompleteSourceMap = mesher.sourceTriangleElementIds();
        bool removedValidMapping = false;
        for (auto& partMap : incompleteSourceMap) {
            for (auto& id : partMap) {
                if (id != 0) { id = 0; removedValidMapping = true; break; }
            }
            if (removedValidMapping) break;
        }
        require(removedValidMapping, "Could not construct incomplete source-map fixture.");
        MeshData incompleteMappingMesh = mesh;
        std::vector<std::string> incompleteMappingDiagnostics;
        require(!AssimpPatchGroupBuilder::apply(model->getRenderMeshes(), patches,
                    incompleteSourceMap, {{"INCOMPLETE_SOURCE_MAP", {connectedPatchIndex}, ""}},
                    incompleteMappingMesh, incompleteMappingDiagnostics),
                "Missing valid source element mapping must be rejected.");
        require(incompleteMappingMesh.getMeshGroupRegistry().find("INCOMPLETE_SOURCE_MAP") == nullptr,
                "Incomplete source mapping partially modified the mesh.");

        auto invalidPatchMetrics = patches;
        invalidPatchMetrics[connectedPatchIndex].area =
            std::numeric_limits<double>::quiet_NaN();
        MeshData invalidPatchMesh = mesh;
        std::vector<std::string> invalidPatchDiagnostics;
        require(!AssimpPatchGroupBuilder::apply(model->getRenderMeshes(), invalidPatchMetrics,
                    mesher.sourceTriangleElementIds(),
                    {{"INVALID_PATCH_METRICS", {connectedPatchIndex}, ""}},
                    invalidPatchMesh, invalidPatchDiagnostics),
                "Non-finite patch metrics must be rejected.");
        require(invalidPatchMesh.getMeshGroupRegistry().find("INVALID_PATCH_METRICS") == nullptr,
                "Invalid patch metadata partially modified the mesh.");

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

        AssimpSurfaceSelector unmatched = plane;
        unmatched.name = "MISSING_BOUNDARY";
        unmatched.point = {{0,0,100}};
        MeshData unmatchedMesh;
        require(!mesher.buildMesh(model, options, {unmatched}, unmatchedMesh),
                "An unmatched FEM surface selector must fail instead of silently omitting its group.");
        require(unmatchedMesh.isEmpty() && mesher.sourceTriangleElementIds().empty(),
                "Failed unmatched-selector import retained partial mesh data.");

        MeshingOptions volumeOptions;
        volumeOptions.dimension = MeshDimension::Volume;
        MeshData rejected;
        require(!mesher.buildMesh(model, volumeOptions, std::vector<AssimpSurfaceSelector>{plane}, rejected),
                "Assimp surface adapter must reject volume-mesh requests.");
        require(mesher.sourceTriangleElementIds().empty(),
                "Rejected Assimp build retained a stale source-element map.");

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
