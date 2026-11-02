#include <OpenModel/AbaqusInpWriter.hpp>
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/ProcessRunner.hpp>
#include <OpenModel/GltfWriter.hpp>
#include <OpenModel/MeshBinaryIO.hpp>
#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>
#include <OpenModel/WebViewWriter.hpp>
#include <OpenModel/FemResults.hpp>
#include <OpenModel/ResultBinding.hpp>
#include <OpenModel/MeshValidator.hpp>
#include <OpenModel/SurfaceMesh.hpp>
#include <OpenModel/Mesher.hpp>
#include <OpenModel/CADModel.hpp>
#include <filesystem>
#ifdef OPENMODEL_HAS_GMSH
#include <OpenModel/GmshMesher.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <cctype>
#include <cstdlib>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
MeshData makeDemoVolumeMesh() {
    MeshData mesh;
    const auto n0 = mesh.addNode(1, 0.0, 0.0, 0.0);
    const auto n1 = mesh.addNode(2, 1.0, 0.0, 0.0);
    const auto n2 = mesh.addNode(3, 0.0, 1.0, 0.0);
    const auto n3 = mesh.addNode(4, 0.0, 0.0, 1.0);
    mesh.addElement("Tetrahedra", ElementType::Tetrahedron4, 1, {n0, n1, n2, n3});
    return mesh;
}


#ifndef OPENMODEL_VERSION
#define OPENMODEL_VERSION "unknown"
#endif

std::string lowerExtension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/** Structured tetrahedral demo block (n cells per side) split into two materials, with a height field. */
MeshData makeDemoGrid(std::size_t n) {
    MeshData mesh;
    const std::size_t s = n + 1;
    auto idx = [s](std::size_t x, std::size_t y, std::size_t z) { return static_cast<NodeIndex>((z * s + y) * s + x); };
    for (std::size_t z = 0; z <= n; ++z)
        for (std::size_t y = 0; y <= n; ++y)
            for (std::size_t x = 0; x <= n; ++x)
                mesh.addNode(static_cast<NodeId>(idx(x, y, z) + 1), double(x) * 2.0 / n, double(y) / n, double(z) / n);
    ElementId id = 1;
    std::vector<ElementId> left, right;
    for (std::size_t z = 0; z < n; ++z)
        for (std::size_t y = 0; y < n; ++y)
            for (std::size_t x = 0; x < n; ++x) {
                const NodeIndex a = idx(x, y, z), b = idx(x + 1, y, z), c = idx(x, y + 1, z), d = idx(x + 1, y + 1, z);
                const NodeIndex e = idx(x, y, z + 1), f = idx(x + 1, y, z + 1), g = idx(x, y + 1, z + 1), h = idx(x + 1, y + 1, z + 1);
                const NodeIndex t[6][4] = {{a, b, d, h}, {a, d, c, h}, {a, c, g, h}, {a, g, e, h}, {a, e, f, h}, {a, f, b, h}};
                const bool isLeft = x < n / 2;
                for (const auto& q : t) {
                    mesh.addElement(isLeft ? "steel" : "aluminium", ElementType::Tetrahedron4, id, {q[0], q[1], q[2], q[3]});
                    (isLeft ? left : right).push_back(id++);
                }
            }
    mesh.addPhysicalGroup("steel", 3, 1, left, "steel");
    mesh.addPhysicalGroup("aluminium", 3, 2, right, "aluminium");
    return mesh;
}

/** Nodal scalar field selected by name: x, y, z or r (distance from the bounding-box centre). */
bool makeField(const MeshData& mesh, const std::string& name, WebNodalField& field) {
    if (name != "x" && name != "y" && name != "z" && name != "r") return false;
    field.name = name == "r" ? "radius" : name;
    std::array<double, 3> lo{{1e300, 1e300, 1e300}}, hi{{-1e300, -1e300, -1e300}};
    for (std::size_t n = 0; n < mesh.getNumNodes(); ++n) {
        const auto c = mesh.nodeCoordinates(n);
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
    }
    field.values.resize(mesh.getNumNodes());
    for (std::size_t n = 0; n < mesh.getNumNodes(); ++n) {
        const auto c = mesh.nodeCoordinates(n);
        if (name == "r") {
            double sum = 0;
            for (int k = 0; k < 3; ++k) { const double d = c[k] - 0.5 * (lo[k] + hi[k]); sum += d * d; }
            field.values[n] = std::sqrt(sum);
        } else {
            field.values[n] = c[name == "x" ? 0 : name == "y" ? 1 : 2];
        }
    }
    return true;
}

bool writeWeb(const MeshData& mesh, const std::string& path, const WebViewOptions& options) {
    const auto result = WebViewWriter::writeFile(path, mesh, options);
    if (!result.success) { std::cerr << "Web export failed: " << result.error << "\n"; return false; }
    std::cout << "Wrote " << result.vertices << " vertices, " << result.triangles << " triangles ("
              << result.boundaryTriangles << " on the skin), " << result.edges << " edges, "
              << result.bytesWritten << " bytes to " << path << "\n";
    return true;
}

SurfaceMesh makeDemoSurfaceMesh() {
    SurfaceMesh mesh;
    SurfaceMeshPart part;
    part.name = "DemoTriangle";
    part.positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    part.normals = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    part.texcoords = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    part.indices = {0, 1, 2};
    mesh.parts.push_back(std::move(part));
    return mesh;
}

#ifdef OPENMODEL_HAS_GMSH
class CadPathModel final : public CADModel {
public:
    explicit CadPathModel(std::string path) : m_path(std::move(path)) {}
    bool loadFromFile(const std::string& path) override { m_path = path; return std::filesystem::is_regular_file(m_path); }
    void tessellate(double) override {}
    const std::vector<RenderSubMesh>& getRenderMeshes() const override { return m_meshes; }
    std::string getFilePath() const override { return m_path; }
private:
    std::string m_path;
    std::vector<RenderSubMesh> m_meshes;
};
#endif

/** Prints the diagnostics of a result import or binding to stderr. */
template <class Diagnostics>
void printDiagnostics(const Diagnostics& diagnostics) {
    for (const auto& d : diagnostics) {
        std::cerr << (d.level == OpenModel::ImportDiagnosticLevel::Error ? "error" : d.level == OpenModel::ImportDiagnosticLevel::Warning ? "warning" : "info")
                  << " " << d.code << ": " << d.message;
        if (d.line) std::cerr << " (line " << d.line << ")";
        std::cerr << "\n";
    }
}

/** Lists the steps and fields of a result file. */
int printResultsInfo(const std::vector<std::filesystem::path>& paths) {
    OpenModel::FemResults results;
    const auto imported = OpenModel::readResultsMerged(paths, results);
    const std::string path = paths.front().string() + (paths.size() > 1 ? " (+" + std::to_string(paths.size() - 1) + " merged)" : "");
    printDiagnostics(imported.diagnostics);
    if (!imported) return 1;
    std::cout << path << ": " << results.format << ", " << results.steps.size() << " step(s), " << results.fields.size() << " field(s)";
    if (!results.modelNodeIds.empty()) std::cout << ", " << results.modelNodeIds.size() << " model nodes";
    std::cout << "\n";
    if (!results.modeSteps().empty()) std::cout << "  " << results.modeSteps().size() << " vibration mode(s)\n";
    if (!results.eigenmodes.empty()) {
        std::cout << "  eigenvalue table (" << results.eigenmodes.size() << " modes; no mode shapes in this file):\n";
        for (const auto& m : results.eigenmodes)
            std::cout << "    mode " << m.mode << ": f = " << m.frequency << " cycles/time, omega = " << m.omega << " rad/time, eigenvalue " << m.eigenvalue << "\n";
    }
    for (std::size_t s = 0; s < results.steps.size(); ++s) {
        std::cout << "  step " << s << ": " << results.steps[s].label;
        if (results.steps[s].kind == OpenModel::StepKind::Frequency)
            std::cout << "  [mode " << results.steps[s].mode << ", frequency " << results.steps[s].value << "]";
        std::cout << "\n";
        for (const auto& f : results.fields) {
            if (f.step != s) continue;
            std::cout << "    " << f.name << " [";
            for (std::size_t c = 0; c < f.components.size(); ++c) std::cout << (c ? "," : "") << f.components[c];
            std::cout << "], " << f.recordCount() << (f.location == OpenModel::ResultLocation::Node ? " nodes" : " elements") << "\n";
        }
    }
    return 0;
}


/** Reads a whole text file; returns false (and prints why) when it cannot be read. */
bool readTextFile(const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::cerr << "Cannot read " << path << "\n"; return false; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

/** Loads a .mgmb or .inp mesh; returns 0 or the process exit code to use. */
int loadMeshFile(const std::string& path, MeshData& mesh) {
    const auto extension = lowerExtension(path);
    if (extension == ".mgmb") {
        try { mesh = MeshBinaryIO::read(path); }
        catch (const std::exception& error) { std::cerr << "Cannot read " << path << ": " << error.what() << "\n"; return 1; }
    } else if (extension == ".inp") {
        OpenModel::AbaqusInpImporter importer;
        const auto imported = importer.read(path, mesh);
        if (!imported) {
            for (const auto& diagnostic : imported.diagnostics) std::cerr << diagnostic.code << ": " << diagnostic.message << "\n";
            return 1;
        }
    } else {
        std::cerr << "Unsupported input format (expected .mgmb or .inp)\n";
        return 2;
    }
    return 0;
}

/** Prints script and writer diagnostics with their line numbers. */
void printAnalysisDiagnostics(const std::vector<OpenModel::ImportDiagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        const char* level = d.level == OpenModel::ImportDiagnosticLevel::Error ? "error" : d.level == OpenModel::ImportDiagnosticLevel::Warning ? "warning" : "info";
        std::cerr << level << (d.line ? " (line " + std::to_string(d.line) + ")" : std::string()) << ": " << d.message << "\n";
    }
}

/** Parses the script and writes the solver input; returns 0 on success. */
int prepareAnalysis(const std::string& meshPath, const std::string& scriptPath, const std::filesystem::path& jobPath) {
    MeshData mesh;
    if (const int code = loadMeshFile(meshPath, mesh)) return code;
    std::string script;
    if (!readTextFile(scriptPath, script)) return 1;
    const auto parsed = OpenModel::parseAnalysisScript(script);
    printAnalysisDiagnostics(parsed.diagnostics);
    if (!parsed.success) return 2;
    std::error_code ec;
    std::filesystem::create_directories(jobPath.parent_path(), ec);
    const auto written = OpenModel::writeSolverInput(mesh, parsed.model, jobPath);
    printAnalysisDiagnostics(written.diagnostics);
    if (!written.success) { std::cerr << "error: " << written.error << "\n"; return 1; }
    std::cout << written.summary << "\nWrote " << jobPath.string() << "\n";
    return 0;
}

void usage() {
    std::cout << "OpenModel " << OPENMODEL_VERSION << "\n"
              << "Usage:\n"
              << "  openmodel info\n"
              << "  openmodel validate-demo\n"
              << "  openmodel demo-inp <output.inp>\n"
              << "  openmodel demo-gltf <output.gltf>\n"
              << "  openmodel demo-glb <output.glb>\n"
              << "  openmodel demo-web <output.mgwv> [cells-per-side]\n"
              << "  openmodel export-web <input.mgmb|input.inp> <output.mgwv> [--no-interior] [--field x|y|z|r]\n"
              << "                     [--results <file.frd|file.dat|file.csv> [--results <more files>] [--step N] [--displacement NAME|none] [--modes N] [--engineering-strain]\n                      [--no-derived] [--no-coordinate-check]]\n"
              << "  openmodel results-info <file.frd|file.dat|file.csv> [more files, merged]\n"
              << "  openmodel write-input <mesh.mgmb|mesh.inp> <analysis.txt> <job.inp>      (analysis script: see docs/SOLVER.md)\n"
              << "  openmodel solve <mesh.mgmb|mesh.inp> <analysis.txt> <workdir> [--solver ccx-path] [--web out.mgwv]\n"
#ifdef OPENMODEL_HAS_GMSH
              << "  openmodel mesh-volume <input.step|input.iges|input.brep> <output.inp|.mgmb|.mgwv> <max-element-size> [order]\n"
#endif
              ;
}
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string command = argv[1];
#ifdef OPENMODEL_HAS_GMSH
    if (command == "mesh-volume") {
        if (argc < 5 || argc > 6) { usage(); return 2; }
        const std::string inputPath = argv[2];
        const std::string outputPath = argv[3];
        double maxSize = 0.0;
        try { maxSize = std::stod(argv[4]); } catch (...) { std::cerr << "Invalid max element size\n"; return 2; }
        int order = 1;
        if (argc == 6) { try { order = std::stoi(argv[5]); } catch (...) { std::cerr << "Invalid element order\n"; return 2; } }
        if (!std::filesystem::is_regular_file(inputPath)) { std::cerr << "Input CAD file does not exist: " << inputPath << "\n"; return 2; }
        if (!(maxSize > 0.0) || !std::isfinite(maxSize) || (order != 1 && order != 2)) {
            std::cerr << "max-element-size must be positive and order must be 1 or 2\n"; return 2;
        }
        auto cad = std::make_shared<CadPathModel>(std::filesystem::absolute(inputPath).string());
        MeshingOptions options; options.dimension = MeshDimension::Volume; options.generateTetrahedra = true;
        options.elementSize = maxSize; options.maxElementSize = maxSize; options.order = order;
        GmshMesher mesher; MeshData mesh;
        if (!mesher.buildMesh(cad, options, mesh)) { std::cerr << "Meshing failed: " << mesher.lastError() << "\n"; return 1; }
        const auto validation = MeshValidator::validate(mesh);
        if (validation.hasErrors()) { std::cerr << "Generated mesh failed validation\n"; for (const auto& issue : validation.issues) if (issue.severity == ValidationSeverity::Error) std::cerr << " - " << issue.message << "\n"; return 1; }
        const auto outputExtension = lowerExtension(outputPath);
        if (outputExtension == ".mgwv") {
            if (!writeWeb(mesh, outputPath, WebViewOptions{})) return 1;
        } else if (outputExtension == ".mgmb") {
            try { MeshBinaryIO::write(outputPath, mesh); }
            catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
        } else {
            const auto written = AbaqusInpWriter::writeFile(outputPath, mesh);
            if (!written.success) { for (const auto& error : written.errors) std::cerr << error << "\n"; return 1; }
        }
        std::cout << "Generated and validated volume mesh: " << mesh.getNumNodes() << " nodes, " << mesh.getNumElements() << " elements. Wrote " << outputPath << "\n";
        return 0;
    }
#endif
    if (command == "info") {
        std::cout << "OpenModel " << OPENMODEL_VERSION << "\n"
                  << "Core API: MeshData, MeshBinaryIO, MeshImporter, FemResults, ResultBinding, MeshValidator, MeshTopology, SurfaceMesh, GltfWriter, WebViewWriter, AbaqusInpWriter\n"
                  << "Build features: static library, shared library (when enabled), CLI\n";
        return 0;
    }
    if (command == "validate-demo") {
        const auto mesh = makeDemoVolumeMesh();
        const auto report = MeshValidator::validate(mesh);
        std::cout << "Nodes: " << mesh.getNumNodes() << "\nElements: " << mesh.getNumElements() << "\n"
                  << "Bounding-box diagonal: " << report.boundingBoxDiagonal << "\n"
                  << "Degenerate elements: " << report.degenerateElements << "\n"
                  << "Unused nodes: " << report.unusedNodes << "\n"
                  << "Minimum triangle angle (deg): " << report.minimumTriangleAngleDegrees << "\n"
                  << "Minimum tetrahedron mean-ratio quality: " << report.minimumTetrahedronQuality << "\n";
        for (const auto& issue : report.issues) {
            std::cout << (issue.severity == ValidationSeverity::Error ? "ERROR: " : "WARNING: ")
                      << issue.message;
            if (!issue.blockName.empty()) std::cout << " [block=" << issue.blockName << "]";
            if (issue.elementId != 0) std::cout << " [element=" << issue.elementId << "]";
            if (issue.code == ValidationCode::NonFiniteCoordinate || issue.code == ValidationCode::UnusedNode)
                std::cout << " [node-index=" << issue.nodeIndex << "]";
            std::cout << "\n";
        }
        if (report.hasErrors()) return 1;
        std::cout << "Validation passed.\n";
        return 0;
    }
    if (command == "write-input") {
        if (argc != 5) { usage(); return 2; }
        return prepareAnalysis(argv[2], argv[3], argv[4]);
    }
    if (command == "solve") {
        if (argc < 5) { usage(); return 2; }
        std::string solver = "ccx", webOut;
        for (int i = 5; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--solver" && i + 1 < argc) solver = argv[++i];
            else if (arg == "--web" && i + 1 < argc) webOut = argv[++i];
            else { usage(); return 2; }
        }
        const std::filesystem::path workDir = std::filesystem::absolute(argv[4]);
        const std::filesystem::path job = workDir / "job.inp";
        if (const int code = prepareAnalysis(argv[2], argv[3], job)) return code;
        std::error_code ec;
        for (const char* ext : {".frd", ".dat", ".sta", ".cvg"}) std::filesystem::remove(workDir / (std::string("job") + ext), ec);
        OpenModel::ProcessRunner runner;
        const auto startError = runner.start({solver, "-i", "job"}, workDir);
        if (!startError.empty()) { std::cerr << startError << "\n"; return 1; }
        // Stream the log while the solver runs.
        for (;;) {
            const auto status = runner.poll();
            for (const auto& line : status.newLines) std::cout << line << "\n";
            std::cout.flush();
            if (status.state != OpenModel::ProcessState::Running) {
                if (status.state != OpenModel::ProcessState::Finished) { std::cerr << "The solver failed (exit code " << status.exitCode << ")\n"; return 1; }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::vector<std::filesystem::path> files;
        for (const char* ext : {".frd", ".dat"}) {
            const auto f = workDir / (std::string("job") + ext);
            if (std::filesystem::is_regular_file(f, ec) && std::filesystem::file_size(f, ec) > 0) files.push_back(f);
        }
        if (files.empty()) { std::cerr << "The solver wrote no results (job.frd missing)\n"; return 1; }
        std::cout << "Results:";
        for (const auto& f : files) std::cout << " " << f.string();
        std::cout << "\n";
        if (!webOut.empty()) {
            MeshData mesh;
            if (const int code = loadMeshFile(argv[2], mesh)) return code;
            OpenModel::FemResults results;
            const auto imported = OpenModel::readResultsMerged(files, results);
            printDiagnostics(imported.diagnostics);
            if (!imported) { std::cerr << "Cannot read the results\n"; return 1; }
            WebViewOptions options;
            OpenModel::BindOptions bind;
            for (const auto& p : files) bind.sourceName += (bind.sourceName.empty() ? "" : " + ") + p.filename().string();
            const auto bound = OpenModel::bindResults(mesh, results, bind, options);
            printDiagnostics(bound.diagnostics);
            if (!bound.success) { std::cerr << "The results do not fit the mesh: " << bound.error << "\n"; return 1; }
            return writeWeb(mesh, webOut, options) ? 0 : 1;
        }
        return 0;
    }
    if (command == "export-web") {
        if (argc < 4) { usage(); return 2; }
        const std::string inputPath = argv[2], outputPath = argv[3];
        WebViewOptions options;
        std::string fieldName;
        std::vector<std::filesystem::path> resultsPaths;
        OpenModel::BindOptions bind;
        bool modesGiven = false, engineeringGiven = false;
        for (int i = 4; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--no-interior") options.includeInteriorFaces = false;
            else if (arg == "--field" && i + 1 < argc) fieldName = argv[++i];
            else if (arg == "--results" && i + 1 < argc) resultsPaths.emplace_back(argv[++i]);
            else if (arg == "--step" && i + 1 < argc) {
                try {
                    std::size_t used = 0;
                    const std::string text = argv[++i];
                    if (!text.empty() && text[0] == '-') throw std::invalid_argument("negative");
                    bind.step = static_cast<std::size_t>(std::stoull(text, &used));
                    if (used != text.size()) throw std::invalid_argument("trailing");
                } catch (...) { std::cerr << "--step needs a non-negative integer\n"; return 2; }
            }
            else if (arg == "--displacement" && i + 1 < argc) bind.displacementField = argv[++i];
            else if (arg == "--modes" && i + 1 < argc) {
                try {
                    std::size_t used = 0;
                    const std::string text = argv[++i];
                    if (!text.empty() && text[0] == '-') throw std::invalid_argument("negative");
                    bind.maxModes = static_cast<std::size_t>(std::stoull(text, &used));
                    if (used != text.size()) throw std::invalid_argument("trailing");
                } catch (...) { std::cerr << "--modes needs a non-negative integer (0 = no modal view)\n"; return 2; }
                modesGiven = true;
            }
            else if (arg == "--engineering-strain") { bind.engineeringShearStrain = true; engineeringGiven = true; }
            else if (arg == "--no-derived") bind.derivedFields = false;
            else if (arg == "--no-coordinate-check") bind.checkCoordinates = false;
            else { usage(); return 2; }
        }
        MeshData mesh;
        const auto extension = lowerExtension(inputPath);
        if (extension == ".mgmb") {
            try { mesh = MeshBinaryIO::read(inputPath); }
            catch (const std::exception& error) { std::cerr << "Cannot read " << inputPath << ": " << error.what() << "\n"; return 1; }
        } else if (extension == ".inp") {
            OpenModel::AbaqusInpImporter importer;
            const auto imported = importer.read(inputPath, mesh);
            if (!imported) {
                for (const auto& diagnostic : imported.diagnostics) std::cerr << diagnostic.code << ": " << diagnostic.message << "\n";
                return 1;
            }
        } else {
            std::cerr << "Unsupported input format (expected .mgmb or .inp)\n";
            return 2;
        }
        if (!fieldName.empty()) {
            WebNodalField field;
            if (!makeField(mesh, fieldName, field)) { std::cerr << "Unknown field '" << fieldName << "' (use x, y, z or r)\n"; return 2; }
            options.fields.push_back(std::move(field));
        }
        if (!resultsPaths.empty()) {
            OpenModel::FemResults results;
            const auto imported = OpenModel::readResultsMerged(resultsPaths, results);
            printDiagnostics(imported.diagnostics);
            if (!imported) { std::cerr << "Cannot read the results\n"; return 1; }
            for (const auto& p : resultsPaths) bind.sourceName += (bind.sourceName.empty() ? "" : " + ") + p.filename().string();
            const auto bound = OpenModel::bindResults(mesh, results, bind, options);
            printDiagnostics(bound.diagnostics);
            if (!bound.success) { std::cerr << "Cannot attach results to the mesh: " << bound.error << "\n"; return 1; }
            std::cout << "Results: step " << bound.step << " (" << bound.stepLabel << "), " << bound.fieldsBound << " nodal fields"
                      << (bound.elementFieldsBound ? ", " + std::to_string(bound.elementFieldsBound) + " element fields" : std::string())
                      << (bound.hasDisplacement ? ", displacement " + bound.displacementName : std::string(", no displacement")) << "\n";
            if (bound.modesBound > 0) {
                std::cout << "Modes:";
                for (std::size_t m = 0; m < bound.modesBound; ++m) std::cout << " " << (m + 1) << "=" << bound.modeFrequencies[m];
                std::cout << "\n";
            } else if (results.steps.size() > 1 && bind.step == OpenModel::BindOptions::kLastStep)
                std::cout << "The file has " << results.steps.size() << " steps; the last one was used (see --step, results-info)\n";
        } else if (bind.step != OpenModel::BindOptions::kLastStep || !bind.displacementField.empty() || !bind.derivedFields || !bind.checkCoordinates ||
                   modesGiven || engineeringGiven) {
            std::cerr << "--step, --displacement, --modes, --engineering-strain, --no-derived and --no-coordinate-check need --results\n";
            return 2;
        }
        return writeWeb(mesh, outputPath, options) ? 0 : 1;
    }
    if (command == "results-info") {
        if (argc < 3) { usage(); return 2; }
        std::vector<std::filesystem::path> files;
        for (int i = 2; i < argc; ++i) files.emplace_back(argv[i]);
        return printResultsInfo(files);
    }
    if (command == "demo-web") {
        if (argc < 3 || argc > 4) { usage(); return 2; }
        std::size_t cells = 10;
        if (argc == 4) {
            try { cells = static_cast<std::size_t>(std::stoul(argv[3])); } catch (...) { cells = 0; }
            if (cells < 2 || cells > 200) { std::cerr << "cells-per-side must be between 2 and 200\n"; return 2; }
        }
        const auto mesh = makeDemoGrid(cells);
        WebViewOptions options;
        WebNodalField field;
        makeField(mesh, "r", field);
        options.fields.push_back(std::move(field));
        return writeWeb(mesh, argv[2], options) ? 0 : 1;
    }
    if (argc < 3) { usage(); return 2; }
    const std::string output = argv[2];
    if (command == "demo-inp") {
        const auto result = AbaqusInpWriter::writeFile(output, makeDemoVolumeMesh());
        if (!result.success) {
            for (const auto& error : result.errors) std::cerr << error << "\n";
            return 1;
        }
        std::cout << "Wrote " << result.nodesWritten << " nodes and " << result.elementsWritten << " elements to " << output << "\n";
        return 0;
    }
    if (command == "demo-gltf" || command == "demo-glb") {
        const auto mesh = makeDemoSurfaceMesh();
        const auto result = command == "demo-glb"
            ? GltfWriter::writeGlbFile(output, mesh)
            : GltfWriter::writeFile(output, mesh);
        if (!result.success) { std::cerr << result.error << "\n"; return 1; }
        std::cout << "Wrote " << result.meshesWritten << " part(s), " << result.verticesWritten
                  << " vertices and " << result.trianglesWritten << " triangles to " << output << "\n";
        return 0;
    }
    usage();
    return 2;
}
