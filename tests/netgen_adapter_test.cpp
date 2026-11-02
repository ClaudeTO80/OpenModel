#include <OpenModel/NetgenMesher.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

class TestCADModel final : public CADModel {
public:
    explicit TestCADModel(std::string path) : m_path(std::move(path)) {}
    bool loadFromFile(const std::string& path) override { m_path = path; return !m_path.empty(); }
    void tessellate(double = 0.1) override {}
    const std::vector<RenderSubMesh>& getRenderMeshes() const override { return m_meshes; }
    std::string getFilePath() const override { return m_path; }
private:
    std::string m_path;
    std::vector<RenderSubMesh> m_meshes;
};

int main() {
    NetgenMesher mesher;
    MeshData output;

    if (mesher.buildMesh({}, MeshingOptions{}, output)) {
        std::cerr << "Null CAD model unexpectedly accepted\n";
        return EXIT_FAILURE;
    }
    if (mesher.lastError().empty()) {
        std::cerr << "Missing diagnostic for null CAD model\n";
        return EXIT_FAILURE;
    }

    auto cad = std::make_shared<TestCADModel>("dummy.step");
    MeshingOptions invalid;
    invalid.elementSize = 0.0;
    if (mesher.buildMesh(cad, invalid, output)) {
        std::cerr << "Invalid element size unexpectedly accepted\n";
        return EXIT_FAILURE;
    }

#if defined(OPENMODEL_NETGEN_TEST_STEP)
    MeshingOptions options;
    options.elementSize = 1.0;
    options.order = 1;
    options.generateTetrahedra = true;
    cad = std::make_shared<TestCADModel>(OPENMODEL_NETGEN_TEST_STEP);
    if (!mesher.buildMesh(cad, options, output)) {
        std::cerr << "Netgen integration failed: " << mesher.lastError() << '\n';
        return EXIT_FAILURE;
    }
    if (output.getNumNodes() == 0 || output.getNumElements() == 0) {
        std::cerr << "Netgen returned an empty mesh\n";
        return EXIT_FAILURE;
    }
#endif

    return EXIT_SUCCESS;
}
