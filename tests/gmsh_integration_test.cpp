#include "support/quadratic_order_check.hpp"
#include <OpenModel/GmshMesher.hpp>
#include <OpenModel/AbaqusInpWriter.hpp>
#include <OpenModel/MeshValidator.hpp>

#include <gmsh.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <set>
#include <string>
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

bool containsElementType(const MeshData& mesh, ElementType type) {
    for (const auto& block : mesh.getElementBlocks())
        if (block.type == type && !block.ids.empty()) return true;
    return false;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "Could not open generated file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}
}

int main() {
    const auto temp = std::filesystem::temp_directory_path();
    const auto stepPath = temp / "openmodel_gmsh_fixture.step";
    const auto inpPath = temp / "openmodel_gmsh_fixture.inp";
    const auto inpQuadraticPath = temp / "openmodel_gmsh_fixture_quadratic.inp";
    std::error_code ec;
    std::filesystem::remove(stepPath, ec);
    std::filesystem::remove(inpPath, ec);
    std::filesystem::remove(inpQuadraticPath, ec);

    try {
        // Produce a genuine STEP file using Gmsh's OpenCASCADE kernel. The
        // adapter then imports that file in its own initialize/finalize session.
        gmsh::initialize();
        try {
            gmsh::option::setNumber("General.Terminal", 0);
            gmsh::model::add("openmodel_fixture_export");
            gmsh::model::occ::addBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
            gmsh::model::occ::synchronize();
            gmsh::write(stepPath.string());
            gmsh::finalize();
        } catch (...) {
            try { gmsh::finalize(); } catch (...) {}
            throw;
        }
        require(std::filesystem::is_regular_file(stepPath) && std::filesystem::file_size(stepPath) > 0,
                "Gmsh did not create a non-empty STEP fixture");

        auto cad = std::make_shared<StepCADModel>(stepPath.string());
        GmshMesher mesher;

        // Log levels: Error by default; at Info and above it holds Gmsh's own messages plus the OpenModel phase
        // traces, and it is also written to the requested file.
        {
            MeshingOptions quiet;
            quiet.dimension = MeshDimension::Volume;
            quiet.elementSize = 0.5;
            quiet.maxElementSize = 0.5;
            MeshData quietMesh;
            require(mesher.buildMesh(cad, quiet, quietMesh), "Quiet meshing failed: " + mesher.lastError());
            require(mesher.log().empty(), "At the default Error level a clean run has an empty log");

            MeshingOptions warning = quiet;
            warning.logLevel = LogLevel::Warning;
            MeshData warningMesh;
            require(mesher.buildMesh(cad, warning, warningMesh), "Warning-level meshing failed: " + mesher.lastError());
            for (const auto& line : mesher.log())
                require(line.find("[OpenModel]") == std::string::npos && line.rfind("Info", 0) != 0,
                        "The Warning level must not record information or trace lines: " + line);

            const auto logPath = temp / "openmodel_gmsh_fixture.log";
            std::filesystem::remove(logPath, ec);
            MeshingOptions verbose = quiet;
            verbose.logLevel = LogLevel::Info;
            verbose.logFile = logPath.string();
            MeshData verboseMesh;
            require(mesher.buildMesh(cad, verbose, verboseMesh), "Verbose meshing failed: " + mesher.lastError());
            const auto hasLine = [&](const std::string& text) {
                for (const auto& line : mesher.log()) if (line.find(text) != std::string::npos) return true;
                return false;
            };
            require(mesher.log().size() > 10, "The verbose log is too short: " + std::to_string(mesher.log().size()));
            require(hasLine("[OpenModel] Start"), "The verbose log has no OpenModel start trace");
            require(hasLine("Gmsh meshing finished"), "The verbose log has no meshing trace");
            require(hasLine("CAD group mapping finished"), "The verbose log has no mapping trace");
            require(hasLine("Meshing 3D") || hasLine("Meshing volume") || hasLine("Tetrahedrization"),
                    "The verbose log has no Gmsh meshing messages");
            require(std::filesystem::is_regular_file(logPath) && std::filesystem::file_size(logPath) > 0,
                    "The verbose log file was not written");
            std::filesystem::remove(logPath, ec);

            // The Debug level adds Gmsh debug messages to the information of the Info level.
            const auto infoLines = mesher.log().size();
            const auto debugLines = [&] {
                std::size_t n = 0;
                for (const auto& line : mesher.log()) if (line.rfind("Debug", 0) == 0) ++n;
                return n;
            };
            require(debugLines() == 0, "The Info level must not record debug messages");
            MeshingOptions debug = verbose;
            debug.logLevel = LogLevel::Debug;
            debug.logFile.clear();
            MeshData debugMesh;
            require(mesher.buildMesh(cad, debug, debugMesh), "Debug-level meshing failed: " + mesher.lastError());
            require(debugLines() > 0 && mesher.log().size() > infoLines, "The Debug level must record more than the Info level");

            // A failed run keeps its log too.
            MeshingOptions broken = verbose;
            broken.elementSize = -1.0;
            MeshData none;
            require(!mesher.buildMesh(cad, broken, none), "A negative element size must be rejected");
        }

        // Linear tetrahedra: generate -> validate -> export INP -> inspect output.
        MeshingOptions linear;
        linear.dimension = MeshDimension::Volume;
        linear.elementSize = 0.35;
        linear.maxElementSize = 0.35;
        linear.minElementSize = 0.0;
        linear.order = 1;
        linear.generateTetrahedra = true;
        MeshData volume;
        require(mesher.buildMesh(cad, linear, volume), "Linear volume meshing failed: " + mesher.lastError());
        require(volume.getNumNodes() > 0, "Linear volume mesh has no nodes");
        require(volume.getNumElements() > 0, "Linear volume mesh has no elements");
        require(containsElementType(volume, ElementType::Tetrahedron4), "Linear mesh contains no Tetrahedron4 block");
        const auto report = MeshValidator::validate(volume);
        if (report.hasErrors()) {
            std::string message = "Linear volume mesh failed MeshValidator:";
            for (const auto& issue : report.issues)
                if (issue.severity == ValidationSeverity::Error) message += "\n - " + issue.message;
            throw std::runtime_error(message);
        }
        require(report.checkedElements > 0, "MeshValidator did not inspect any elements");
        require(report.minimumTetrahedronQuality > 0.0, "Minimum tetrahedron quality was not computed");

        const auto writeLinear = AbaqusInpWriter::writeFile(inpPath.string(), volume);
        require(writeLinear.success, "Linear INP export failed");
        require(writeLinear.nodesWritten == volume.getNumNodes(), "INP node count differs from the source mesh");
        require(writeLinear.elementsWritten == volume.getNumElements(), "INP element count differs from the source mesh");
        const auto inp = readFile(inpPath);
        require(inp.find("*NODE") != std::string::npos, "INP output is missing *NODE");
        require(inp.find("*ELEMENT, TYPE=C3D4") != std::string::npos, "INP output is missing C3D4 elements");

        {
            // Volume -> solid assignment: the solids found at most sample points win. A coarse element
            // spanning a bore has its centroid in the solid that fills the bore (a nut around a rod).
            bool agree = true;
            using Set = std::set<int>;
            require(GmshMesher::solidsByMajority({{10}, {10}, {8}, {10}, {10}}, agree) == Set{10} && !agree,
                    "An outlier sample outvoted the solid of the volume");
            require(GmshMesher::solidsByMajority({{8}, {10}, {10}, {10}}, agree) == Set{10},
                    "The first sample decided the solid of the volume");
            require(GmshMesher::solidsByMajority({{9, 11}, {9, 11}, {11}}, agree) == Set{11},
                    "A solid overlapping part of the volume was kept");
            require(GmshMesher::solidsByMajority({{3, 4}, {3, 4}}, agree) == Set{3, 4} && agree,
                    "Coincident solids were not both kept");
            require(GmshMesher::solidsByMajority({{}, {}}, agree).empty(), "A volume outside every solid was matched");
            require(GmshMesher::solidsByMajority({{}, {5}}, agree) == Set{5},
                    "A sample outside every solid hid the matching solid");
        }

        // Quadratic tetrahedra: verify high-order connectivity survives extraction
        // and the Abaqus writer maps it to C3D10.
        MeshingOptions quadratic = linear;
        quadratic.order = 2;
        MeshData quadraticVolume;
        require(mesher.buildMesh(cad, quadratic, quadraticVolume),
                "Quadratic volume meshing failed: " + mesher.lastError());
        require(containsElementType(quadraticVolume, ElementType::Tetrahedron10),
                "Quadratic mesh contains no Tetrahedron10 block");
        const auto quadraticReport = MeshValidator::validate(quadraticVolume);
        require(!quadraticReport.hasErrors(), "Quadratic volume mesh failed MeshValidator");
        {
            // Gmsh orders the last two mid-edge nodes of a Tetrahedron10 differently from Abaqus.
            std::size_t tets = 0, tris = 0;
            const double deviation = quadratic_order::maxMidNodeDeviation(quadraticVolume, tets, tris);
            require(tets > 0, "No Tetrahedron10 elements to check");
            require(deviation < 1e-9, "Quadratic mid-edge nodes are not in the Abaqus order (deviation " +
                                          std::to_string(deviation) + ")");
        }
        const auto writeQuadratic = AbaqusInpWriter::writeFile(inpQuadraticPath.string(), quadraticVolume);
        require(writeQuadratic.success, "Quadratic INP export failed");
        require(readFile(inpQuadraticPath).find("*ELEMENT, TYPE=C3D10") != std::string::npos,
                "Quadratic INP output is missing C3D10 elements");

        // Surface-only mode must return triangles and no volume tetrahedra.
        MeshingOptions surface = linear;
        surface.dimension = MeshDimension::Surface;
        surface.generateTetrahedra = false;
        MeshData surfaceMesh;
        require(mesher.buildMesh(cad, surface, surfaceMesh), "Surface meshing failed: " + mesher.lastError());
        require(containsElementType(surfaceMesh, ElementType::Triangle3), "Surface mesh contains no Triangle3 block");
        require(!containsElementType(surfaceMesh, ElementType::Tetrahedron4), "Surface-only mesh unexpectedly contains tetrahedra");
        require(!MeshValidator::validate(surfaceMesh).hasErrors(), "Surface mesh failed MeshValidator");

        // Invalid options must fail transactionally, preserving existing output.
        const auto oldNodeCount = volume.getNumNodes();
        const auto oldElementCount = volume.getNumElements();
        MeshingOptions invalid = linear;
        invalid.elementSize = -1.0;
        require(!mesher.buildMesh(cad, invalid, volume), "Invalid options unexpectedly succeeded");
        require(volume.getNumNodes() == oldNodeCount && volume.getNumElements() == oldElementCount,
                "Failed meshing modified the caller's existing output mesh");
        require(!mesher.lastError().empty(), "Failed meshing did not provide a diagnostic");

        std::cout << "Gmsh end-to-end integration passed: linear=" << writeLinear.elementsWritten
                  << " elements, quadratic=" << writeQuadratic.elementsWritten
                  << " elements, surface=" << surfaceMesh.getNumElements() << " elements\n";
    } catch (const std::exception& error) {
        std::cerr << "Gmsh end-to-end integration failed: " << error.what() << '\n';
        std::filesystem::remove(stepPath, ec);
        std::filesystem::remove(inpPath, ec);
        std::filesystem::remove(inpQuadraticPath, ec);
        return 1;
    }

    std::filesystem::remove(stepPath, ec);
    std::filesystem::remove(inpPath, ec);
    std::filesystem::remove(inpQuadraticPath, ec);
    return 0;
}
