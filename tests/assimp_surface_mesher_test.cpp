#include <OpenModel/AssimpSurfaceMesher.hpp>
#include <OpenModel/GltfWriter.hpp>

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
        part.name = "TwoPlanes";
        part.positions = {
            0,0,0,  1,0,0,  0,1,0,       // selected z=0 triangle
            0,0,1,  1,0,1,  0,1,1        // unselected z=1 triangle
        };
        part.indices = {0,1,2, 3,4,5};
        fixture.parts.push_back(part);
        const auto written = GltfWriter::writeGlbFile(path.string(), fixture);
        require(written.success, "Could not create GLB fixture: " + written.error);

        auto model = std::make_shared<AssimpModel>();
        require(model->loadFromFile(path.string()), "Assimp load failed: " + model->getLastError());

        MeshingOptions options;
        options.dimension = MeshDimension::Surface;
        AssimpPlaneSurfaceGroup clamp;
        clamp.name = "FIXED_END";
        clamp.point = {{0,0,0}};
        clamp.normal = {{0,0,1}};
        clamp.tolerance = 1e-6;
        clamp.minNormalAlignment = 0.99;

        AssimpSurfaceMesher mesher;
        MeshData mesh;
        require(mesher.buildMesh(model, options, {clamp}, mesh),
                "Assimp surface meshing failed.");
        const auto* group = mesh.getMeshGroupRegistry().find("FIXED_END");
        require(group != nullptr, "Plane-selected group was not created.");
        require(group->source == MeshGroupSource::Imported && group->dimension == 2,
                "Imported group metadata is incorrect.");
        require(group->elementIds.size() == 1, "Plane selector did not isolate one triangle.");
        require(group->nodeIndices.size() == 3, "Plane selector did not create a 3-node set.");
        const auto nodeSet = mesh.getNodeSets().find("FIXED_END_NODES");
        require(nodeSet != mesh.getNodeSets().end() && nodeSet->second.size() == 3,
                "Solver-ready node set is missing.");
        require(mesh.getNumElements() == 2, "Surface import did not preserve both triangles.");

        MeshingOptions volumeOptions;
        volumeOptions.dimension = MeshDimension::Volume;
        MeshData rejected;
        require(!mesher.buildMesh(model, volumeOptions, {clamp}, rejected),
                "Assimp surface adapter must reject volume-mesh requests.");

        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::cout << "Assimp surface selection passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
