#include <OpenModel/AssimpModel.hpp>
#include <OpenModel/GltfWriter.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() / "openmodel_assimp_roundtrip_fixture.glb";
    try {
        SurfaceMesh source;
        SurfaceMeshPart part;
        part.name = "AssimpIntegrationTriangle";
        part.positions = {0.0f, 0.0f, 0.0f,
                          1.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f};
        part.texcoords = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        part.baseColorFactor = {{0.2f, 0.4f, 0.8f, 1.0f}};
        part.metallicFactor = 0.25f;
        part.roughnessFactor = 0.35f;
        part.indices = {0, 1, 2};
        source.parts.push_back(part);

        const auto written = GltfWriter::writeGlbFile(path.string(), source);
        require(written.success, "Could not create GLB integration fixture: " + written.error);
        require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) > 20,
                "GLB fixture is missing or empty");

        AssimpModel model;
        require(model.loadFromFile(path.string()), "Assimp failed to load generated GLB: " + model.getLastError());
        require(!model.getRenderMeshes().empty(), "Assimp returned no render meshes");

        std::size_t vertices = 0;
        std::size_t triangles = 0;
        for (const auto& mesh : model.getRenderMeshes()) {
            require(mesh.vertices.size() % 3 == 0, "Imported vertex array is not xyz-packed");
            require(mesh.normals.empty() || mesh.normals.size() == mesh.vertices.size(),
                    "Imported normal count does not match vertex count");
            require(mesh.indices.size() % 3 == 0, "Imported index array is not triangle-packed");
            const auto vertexCount = mesh.vertices.size() / 3;
            for (const auto index : mesh.indices)
                require(index < vertexCount, "Imported index is outside the vertex range");
            for (const auto coordinate : mesh.vertices)
                require(std::isfinite(coordinate), "Imported coordinate is not finite");
            vertices += vertexCount;
            triangles += mesh.indices.size() / 3;
        }
        require(vertices >= 3 && triangles >= 1, "Assimp round-trip produced no usable triangles");

        // The importer should report a useful diagnostic and clear stale geometry
        // after a subsequent failed load.
        require(!model.loadFromFile((path.string() + ".missing")), "Assimp unexpectedly loaded a missing file");
        require(!model.getLastError().empty(), "Failed Assimp load did not provide a diagnostic");
        require(model.getRenderMeshes().empty(), "Failed Assimp load retained stale render meshes");

        std::cout << "Assimp integration passed: " << vertices << " vertices, "
                  << triangles << " triangles\n";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Assimp integration failed: " << error.what() << '\n';
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return 1;
    }
}
