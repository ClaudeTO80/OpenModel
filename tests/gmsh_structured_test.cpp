#include <OpenModel/GmshMesher.hpp>
#include <OpenModel/MeshQuality.hpp>

#include <gmsh.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class StepCADModel final : public CADModel {
public:
    explicit StepCADModel(std::string path) : path_(std::move(path)) {}
    bool loadFromFile(const std::string& filepath) override { path_ = filepath; return true; }
    void tessellate(double = 0.1) override {}
    const std::vector<RenderSubMesh>& getRenderMeshes() const override { return meshes_; }
    std::string getFilePath() const override { return path_; }
private:
    std::string path_;
    std::vector<RenderSubMesh> meshes_;
};

/** Write a STEP file produced by Gmsh's OpenCASCADE kernel. */
template <typename Builder>
std::shared_ptr<CADModel> makeStep(const std::filesystem::path& path, Builder build) {
    gmsh::initialize();
    try {
        gmsh::option::setNumber("General.Terminal", 0);
        gmsh::model::add("fixture");
        build();
        gmsh::model::occ::synchronize();
        gmsh::write(path.string());
        gmsh::finalize();
    } catch (...) {
        try { gmsh::finalize(); } catch (...) {}
        throw;
    }
    require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) > 0, "STEP fixture was not written");
    return std::make_shared<StepCADModel>(path.string());
}

std::size_t countType(const MeshData& mesh, ElementType type) {
    std::size_t n = 0;
    for (const auto& block : mesh.getElementBlocks()) if (block.type == type) n += block.size();
    return n;
}

std::size_t countVolumeElements(const MeshData& mesh) {
    std::size_t n = 0;
    for (const auto& block : mesh.getElementBlocks()) if (elementDimension(block.type) == 3) n += block.size();
    return n;
}

bool hasDiagnostic(const GmshMesher& mesher, const std::string& text) {
    for (const auto& line : mesher.diagnostics()) if (line.find(text) != std::string::npos) return true;
    return false;
}

/** Every mid-edge node of a straight-sided element must lie at the middle of its edge in the OpenModel (Abaqus) order. */
void requireMidNodesOnEdges(const MeshData& mesh, ElementType type, const std::vector<std::array<int, 2>>& edges,
                            std::size_t firstMidNode, const std::string& label) {
    std::size_t checked = 0;
    for (const auto& block : mesh.getElementBlocks()) {
        if (block.type != type) continue;
        for (std::size_t e = 0; e < block.size(); ++e) {
            const auto* nodes = &block.connectivity[e * block.nodesPerElement];
            for (std::size_t k = 0; k < edges.size(); ++k) {
                const auto a = mesh.nodeCoordinates(nodes[edges[k][0]]);
                const auto b = mesh.nodeCoordinates(nodes[edges[k][1]]);
                const auto m = mesh.nodeCoordinates(nodes[firstMidNode + k]);
                for (int c = 0; c < 3; ++c)
                    require(std::abs(m[static_cast<std::size_t>(c)] - 0.5 * (a[static_cast<std::size_t>(c)] + b[static_cast<std::size_t>(c)])) < 1e-9,
                            label + ": mid-edge node " + std::to_string(firstMidNode + k) + " is not at the middle of edge " +
                            std::to_string(edges[k][0]) + "-" + std::to_string(edges[k][1]));
            }
            ++checked;
        }
    }
    require(checked > 0, label + ": no element of the expected type");
}
}

int main() {
    try {
        const auto temp = std::filesystem::temp_directory_path();
        // A 2 x 1 x 6 bar: a straight prism. A sphere: not a prism.
        const auto bar = makeStep(temp / "openmodel_structured_bar.step", [] { gmsh::model::occ::addBox(0, 0, 0, 2, 1, 6); });
        const auto sphere = makeStep(temp / "openmodel_structured_sphere.step", [] { gmsh::model::occ::addSphere(0, 0, 0, 1); });
        // A 100 x 2 x 100 plate: its 100 x 2 side faces are thin strips.
        const auto plate = makeStep(temp / "openmodel_structured_plate.step", [] { gmsh::model::occ::addBox(0, 0, 0, 100, 2, 100); });
        GmshMesher mesher;
        MeshingOptions base;
        base.dimension = MeshDimension::Volume;
        base.elementSize = 1.5;
        base.maxElementSize = 1.5;
        base.importCadGroups = false;

        // Hexahedra: the bar is swept in ceil(6 / 1.5) = 4 layers.
        {
            MeshingOptions options = base;
            options.structure = MeshStructure::Structured;
            options.structuredShape = StructuredElementShape::Hexahedron;
            MeshData mesh;
            require(mesher.buildMesh(bar, options, mesh), "Structured hexahedral meshing failed: " + mesher.lastError());
            require(countType(mesh, ElementType::Tetrahedron4) == 0, "A structured mesh must contain no tetrahedra");
            const auto hexahedra = countType(mesh, ElementType::Hexahedron8);
            require(hexahedra > 0, "The recombined end face must give hexahedra");
            require(countVolumeElements(mesh) % 4 == 0, "The elements must come in 4 layers: " + std::to_string(countVolumeElements(mesh)));
            require(hexahedra + countType(mesh, ElementType::Prism6) == countVolumeElements(mesh),
                    "A hexahedral sweep contains hexahedra and prisms only");
            const auto quality = computeMeshQuality(mesh);
            require(quality.hasVolumeElements && quality.acceptable(), "The swept bar must have no poor element: " + quality.summary());
            require(quality.minimumVolumeQuality > 0.7, "The swept bar must keep right angles: " + quality.summary());
            require(hasDiagnostic(mesher, "Structured mesh:"), "A structured run reports the sweep");
            require(mesh.validate().empty(), "The swept mesh failed structural validation");
        }

        // Prisms.
        {
            MeshingOptions options = base;
            options.structure = MeshStructure::Structured;
            options.structuredShape = StructuredElementShape::Prism;
            MeshData mesh;
            require(mesher.buildMesh(bar, options, mesh), "Structured prismatic meshing failed: " + mesher.lastError());
            require(countType(mesh, ElementType::Prism6) > 0 && countType(mesh, ElementType::Prism6) == countVolumeElements(mesh),
                    "A prismatic sweep contains prisms only");
            require(computeMeshQuality(mesh).acceptable(), "The prismatic bar must have no poor element");
        }

        // Second order: 20-node hexahedra and 15-node prisms in OpenModel (Abaqus) node order.
        {
            MeshingOptions options = base;
            options.structure = MeshStructure::Structured;
            options.order = 2;
            options.structuredShape = StructuredElementShape::Hexahedron;
            MeshData mesh;
            require(mesher.buildMesh(bar, options, mesh), "Quadratic hexahedral meshing failed: " + mesher.lastError());
            require(countType(mesh, ElementType::Hexahedron27) == 0 && countType(mesh, ElementType::Hexahedron20) > 0,
                    "Second-order hexahedra must have 20 nodes");
            requireMidNodesOnEdges(mesh, ElementType::Hexahedron20,
                {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}, 8, "Hexahedron20");
            if (countType(mesh, ElementType::Prism15) > 0)
                requireMidNodesOnEdges(mesh, ElementType::Prism15,
                    {{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}, {0, 3}, {1, 4}, {2, 5}}, 6, "Prism15 of a hexahedral sweep");
            options.structuredShape = StructuredElementShape::Prism;
            MeshData prisms;
            require(mesher.buildMesh(bar, options, prisms), "Quadratic prismatic meshing failed: " + mesher.lastError());
            requireMidNodesOnEdges(prisms, ElementType::Prism15,
                {{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}, {0, 3}, {1, 4}, {2, 5}}, 6, "Prism15");
            require(computeMeshQuality(prisms).acceptable(), "The quadratic prisms must not be inverted");
        }

        // A sphere is not a prism: fall back to tetrahedra, or fail when the fallback is off.
        {
            MeshingOptions options = base;
            options.elementSize = options.maxElementSize = 0.5;
            options.structure = MeshStructure::Structured;
            MeshData mesh;
            require(mesher.buildMesh(sphere, options, mesh), "The fallback must produce a tetrahedral mesh: " + mesher.lastError());
            require(countType(mesh, ElementType::Tetrahedron4) > 0, "The fallback mesh has no tetrahedra");
            require(hasDiagnostic(mesher, "not possible"), "The fallback must be explained");
            options.structuredFallback = false;
            MeshData none;
            require(!mesher.buildMesh(sphere, options, none), "Without the fallback a sphere cannot be meshed in a structured way");
            require(mesher.lastError().find("straight prism") != std::string::npos, "The error must name the reason: " + mesher.lastError());
        }

        // Thin faces: the 100 x 2 side faces of the plate get needle triangles unless their size is limited.
        {
            MeshingOptions options = base;
            options.elementSize = options.maxElementSize = 40.0;
            MeshData coarse;
            require(mesher.buildMesh(plate, options, coarse), "Plate meshing failed: " + mesher.lastError());
            const double without = computeMeshQuality(coarse).minimumSurfaceQuality;
            options.thinFaceSizeFactor = 1.0;
            MeshData refined;
            require(mesher.buildMesh(plate, options, refined), "Thin-face meshing failed: " + mesher.lastError());
            const auto quality = computeMeshQuality(refined);
            require(hasDiagnostic(mesher, "Thin-face sizing"), "The thin-face limit must be reported");
            require(quality.minimumSurfaceQuality > without,
                    "Limiting thin faces must raise the worst surface quality: " + std::to_string(without) + " -> " +
                    std::to_string(quality.minimumSurfaceQuality));
            require(quality.minimumSurfaceQuality > 0.3, "Thin faces are still badly meshed: " + quality.summary());
            options.thinFaceSizeFactor = -1.0;
            MeshData none;
            require(!mesher.buildMesh(plate, options, none), "A negative thin-face factor must be rejected");
        }
        std::cout << "gmsh_structured_test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "gmsh_structured_test failed: " << e.what() << "\n";
        return 1;
    }
}
