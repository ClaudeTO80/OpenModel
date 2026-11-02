#include <OpenModel/TetGenMesher.hpp>
#include <OpenModel/MeshValidator.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class TetrahedronSurface final : public CADModel {
public:
    TetrahedronSurface() {
        RenderSubMesh mesh;
        mesh.name = "ClosedTetrahedron";
        mesh.vertices = {
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };
        // Four consistently connected triangular faces, indices are zero-based.
        mesh.indices = {
            0, 2, 1,
            0, 1, 3,
            1, 2, 3,
            2, 0, 3
        };
        meshes_.push_back(std::move(mesh));
    }
    bool loadFromFile(const std::string& path) override { path_ = path; return !path.empty(); }
    void tessellate(double = 0.1) override {}
    const std::vector<RenderSubMesh>& getRenderMeshes() const override { return meshes_; }
    std::string getFilePath() const override { return path_; }
private:
    std::string path_ = "in-memory-tetrahedron";
    std::vector<RenderSubMesh> meshes_;
};

bool hasType(const MeshData& mesh, ElementType type) {
    for (const auto& block : mesh.getElementBlocks())
        if (block.type == type && !block.ids.empty()) return true;
    return false;
}
}

int main() {
    try {
        auto cad = std::make_shared<TetrahedronSurface>();
        TetGenMesher mesher;
        MeshingOptions options;
        options.dimension = MeshDimension::Volume;
        options.generateTetrahedra = true;
        options.order = 1;
        options.elementSize = 0.75;

        MeshData volume;
        require(mesher.buildMesh(cad, options, volume), "TetGen meshing failed");
        require(volume.getNumNodes() >= 4, "TetGen returned fewer than four nodes");
        require(volume.getNumElements() > 0, "TetGen returned no elements");
        require(hasType(volume, ElementType::Tetrahedron4), "TetGen returned no linear tetrahedra");
        const auto structuralErrors = volume.validate();
        require(structuralErrors.empty(), "TetGen output failed structural validation: " +
                (structuralErrors.empty() ? std::string{} : structuralErrors.front()));
        const auto report = MeshValidator::validate(volume);
        if (report.hasErrors()) {
            std::string details;
            for (const auto& issue : report.issues)
                if (issue.severity == ValidationSeverity::Error) details += "\n - " + issue.message;
            throw std::runtime_error("TetGen output failed geometric validation:" + details);
        }

        MeshData unchanged;
        unchanged.addNode(999, 9.0, 9.0, 9.0);
        auto invalid = options;
        invalid.elementSize = 0.0;
        require(!mesher.buildMesh(cad, invalid, unchanged), "TetGen accepted an invalid element size");

        std::cout << "TetGen integration passed: " << volume.getNumNodes() << " nodes, "
                  << volume.getNumElements() << " tetrahedra\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "TetGen integration failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
