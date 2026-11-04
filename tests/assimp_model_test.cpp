#include <OpenModel/AssimpModel.hpp>
#include <OpenModel/GltfWriter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        require(model.getRenderMeshes().front().sourcePartId >= 0,
                "Assimp source mesh identifier was not preserved.");
        const auto& importedMaterial = model.getRenderMeshes().front();
        require(std::abs(importedMaterial.baseColorFactor[0] - 0.2f) < 1e-3f &&
                std::abs(importedMaterial.baseColorFactor[1] - 0.4f) < 1e-3f &&
                std::abs(importedMaterial.baseColorFactor[2] - 0.8f) < 1e-3f,
                "Assimp base-color material factors were not imported.");
        require(std::abs(importedMaterial.metallicFactor - 0.25f) < 1e-3f &&
                std::abs(importedMaterial.roughnessFactor - 0.35f) < 1e-3f,
                "Assimp metallic-roughness material factors were not imported.");

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

        // A negative node scale is a mirrored transform. The importer must
        // reverse triangle winding so geometric normals remain consistent
        // with inverse-transpose transformed vertex normals.
        const auto gltfPath = std::filesystem::temp_directory_path() / "openmodel_assimp_mirrored_fixture.gltf";
        const auto gltfWritten = GltfWriter::writeFile(gltfPath.string(), source);
        require(gltfWritten.success, "Could not create mirrored glTF fixture: " + gltfWritten.error);
        {
            std::ifstream input(gltfPath, std::ios::binary);
            std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            const std::string node = R"glb("mesh":0})glb";
            const auto position = json.find(node);
            require(position != std::string::npos, "Could not locate glTF mesh node for mirrored fixture.");
            json.replace(position, node.size(), R"glb("mesh":0,"scale":[-1,1,1]})glb");
            std::ofstream output(gltfPath, std::ios::binary | std::ios::trunc);
            output.write(json.data(), static_cast<std::streamsize>(json.size()));
            require(static_cast<bool>(output), "Could not write mirrored glTF fixture.");
        }
        AssimpModel mirroredModel;
        require(mirroredModel.loadFromFile(gltfPath.string()),
                "Assimp failed to load mirrored glTF: " + mirroredModel.getLastError());
        require(!mirroredModel.getRenderMeshes().empty(), "Mirrored glTF produced no render mesh.");
        const auto& mirroredMesh = mirroredModel.getRenderMeshes().front();
        require(mirroredMesh.indices.size() >= 3 &&
                mirroredMesh.indices[0] == 0 &&
                mirroredMesh.indices[1] == 2 &&
                mirroredMesh.indices[2] == 1,
                "Negative-determinant node transform did not reverse triangle winding.");
        if (!mirroredMesh.normals.empty())
            require(mirroredMesh.normals[2] > 0.99f,
                    "Mirrored transform produced an inconsistent vertex normal.");

        AssimpModel scaledModel;
        AssimpImportOptions importOptions;
        importOptions.coordinateScale = 1000.0;
        std::string optionError;
        require(scaledModel.setImportOptions(importOptions, &optionError),
                "Valid Assimp import options were rejected: " + optionError);
        require(scaledModel.loadFromFile(path.string()),
                "Scaled Assimp import failed: " + scaledModel.getLastError());
        double maximumCoordinate = 0.0;
        for (const auto coordinate : scaledModel.getRenderMeshes().front().vertices)
            maximumCoordinate = std::max(maximumCoordinate, std::abs(static_cast<double>(coordinate)));
        require(std::abs(maximumCoordinate - 1000.0) < 1e-3,
                "Assimp coordinateScale was not applied to transformed geometry.");
        AssimpImportOptions invalidOptions;
        invalidOptions.coordinateScale = 0.0;
        require(!scaledModel.setImportOptions(invalidOptions, &optionError) && !optionError.empty(),
                "Invalid Assimp coordinateScale was accepted.");

        // The importer should report a useful diagnostic and clear stale geometry
        // after a subsequent failed load.
        require(!model.loadFromFile((path.string() + ".missing")), "Assimp unexpectedly loaded a missing file");
        require(!model.getLastError().empty(), "Failed Assimp load did not provide a diagnostic");
        require(model.getRenderMeshes().empty(), "Failed Assimp load retained stale render meshes");

        std::cout << "Assimp integration passed: " << vertices << " vertices, "
                  << triangles << " triangles\n";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(std::filesystem::temp_directory_path() / "openmodel_assimp_mirrored_fixture.gltf", ec);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Assimp integration failed: " << error.what() << '\n';
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return 1;
    }
}
