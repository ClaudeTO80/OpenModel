#include <OpenModel/ElmerSolver.hpp>
#include <OpenModel/Solvers.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <system_error>
#include <unordered_map>

/**
 * @file ElmerSolver.cpp
 * @brief Elmer case export (mesh directory and SIF) for static analyses, and the launch helpers.
 */
namespace OpenModel {
namespace {

namespace fs = std::filesystem;

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool validJobName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

std::string realText(double v) {
    std::ostringstream out;
    out << std::setprecision(15) << v;
    return out.str();
}

/** Element code of the ElmerSolver format, 0 when the type has none here. */
int elmerCode(ElementType type) {
    switch (type) {
    case ElementType::Point1: return 101;
    case ElementType::Line2: return 202;
    case ElementType::Triangle3: return 303;
    case ElementType::Triangle6: return 306;
    case ElementType::Quadrilateral4: return 404;
    case ElementType::Quadrilateral8: return 408;
    case ElementType::Quadrilateral9: return 409;
    case ElementType::Tetrahedron4: return 504;
    case ElementType::Tetrahedron10: return 510;
    case ElementType::Hexahedron8: return 808;
    case ElementType::Hexahedron20: return 820;
    case ElementType::Prism6: return 706;
    case ElementType::Prism15: return 715;
    case ElementType::Pyramid5: return 605;
    default: return 0;
    }
}

/** Local corner numbers of the faces of a volume element (corners first; mid-side nodes are not faces). */
std::vector<std::vector<int>> volumeFaces(ElementType type) {
    switch (type) {
    case ElementType::Tetrahedron4:
    case ElementType::Tetrahedron10: return {{0, 1, 2}, {0, 1, 3}, {1, 2, 3}, {0, 2, 3}};
    case ElementType::Hexahedron8:
    case ElementType::Hexahedron20: return {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    case ElementType::Prism6:
    case ElementType::Prism15: return {{0, 1, 2}, {3, 4, 5}, {0, 1, 4, 3}, {1, 2, 5, 4}, {2, 0, 3, 5}};
    case ElementType::Pyramid5: return {{0, 1, 2, 3}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
    default: return {};
    }
}

/** Sorted corner nodes: the key that identifies a face whatever its orientation. */
std::vector<NodeIndex> faceKey(const std::vector<NodeIndex>& corners) {
    std::vector<NodeIndex> key = corners;
    std::sort(key.begin(), key.end());
    return key;
}

const MeshPhysicalGroup* findPhysical(const MeshData& mesh, const std::string& name) {
    for (const auto& g : mesh.getPhysicalGroups()) if (g.name == name) return &g;
    for (const auto& g : mesh.getPhysicalGroups()) if (lowered(g.name) == lowered(name)) return &g;
    return nullptr;
}

ElmerExportResult fail(ElmerExportResult r, std::string message) {
    r.success = false;
    r.error = std::move(message);
    return r;
}

void note(ElmerExportResult& r, ImportDiagnosticLevel level, std::string code, std::string message) {
    r.diagnostics.push_back({level, std::move(code), std::move(message), 0});
}

/** A volume element of the export, with its Elmer id (1-based) and body (1-based). */
struct VolumeElement {
    std::size_t id = 0;
    std::size_t body = 0;
    int code = 0;
    std::vector<NodeIndex> nodes;
};

/** A boundary element: a point element of a fix or a face element of a pressure, with the tag of its boundary. */
struct BoundaryElement {
    int tag = 0;
    std::size_t parent = 0;     ///< Elmer id of the volume element it belongs to.
    int code = 0;
    std::vector<NodeIndex> nodes;
};

/** Writes the four mesh files of the ElmerSolver directory format. */
bool writeMeshDirectory(const fs::path& directory, const MeshData& mesh, const std::vector<VolumeElement>& volumes,
                        const std::vector<BoundaryElement>& boundaries, std::string& error) {
    std::ofstream header(directory / "mesh.header"), nodes(directory / "mesh.nodes"),
        elements(directory / "mesh.elements"), boundary(directory / "mesh.boundary");
    if (!header || !nodes || !elements || !boundary) {
        error = "cannot write the mesh files in " + directory.string();
        return false;
    }
    nodes << std::setprecision(15);
    for (NodeIndex i = 0; i < mesh.getNumNodes(); ++i) {
        const auto c = mesh.nodeCoordinates(i);
        nodes << (i + 1) << " -1 " << c[0] << ' ' << c[1] << ' ' << c[2] << '\n';
    }
    std::map<int, std::size_t> counts;
    for (const auto& v : volumes) {
        elements << v.id << ' ' << v.body << ' ' << v.code;
        for (const NodeIndex n : v.nodes) elements << ' ' << (n + 1);
        elements << '\n';
        ++counts[v.code];
    }
    for (std::size_t i = 0; i < boundaries.size(); ++i) {
        const auto& b = boundaries[i];
        boundary << (i + 1) << ' ' << b.tag << ' ' << b.parent << " 0 " << b.code;
        for (const NodeIndex n : b.nodes) boundary << ' ' << (n + 1);
        boundary << '\n';
    }
    header << mesh.getNumNodes() << ' ' << volumes.size() << ' ' << boundaries.size() << '\n';
    header << counts.size() << '\n';
    for (const auto& [code, count] : counts) header << code << ' ' << count << '\n';
    return true;
}

/** Simulation solver input for a static linear-elastic analysis. */
void writeSif(std::ostream& out, const std::string& stem, const std::vector<const AnalysisMaterial*>& materials,
              const std::vector<std::size_t>& bodyMaterial, const std::vector<bool>& bodyGravity,
              const std::vector<std::pair<std::size_t, std::string>>& bodyForces,
              const std::vector<std::pair<int, std::array<bool, 3>>>& fixes, const std::vector<std::array<double, 3>>& fixValues,
              const std::vector<std::pair<int, double>>& pressures) {
    out << "! Generated by OpenModel. Static linear elasticity (StressSolve). Units: the model's own.\n";
    out << "Header\n  Mesh DB \".\" \".\"\n  Include Path \"\"\nEnd\n\n";
    out << "Simulation\n  Coordinate System = Cartesian 3D\n  Simulation Type = Steady state\n"
        << "  Steady State Max Iterations = 1\n  Output Intervals = 1\n  Output File = \"" << stem << ".result\"\n"
        << "  Post File = \"" << stem << ".vtu\"\nEnd\n\n";
    out << "Constants\n  Gravity(4) = 0 0 -1 9.82\nEnd\n\n";
    for (std::size_t b = 0; b < bodyMaterial.size(); ++b) {
        out << "Body " << (b + 1) << "\n  Equation = 1\n  Material = " << bodyMaterial[b] << "\n";
        if (bodyGravity[b]) out << "  Body Force = " << (b + 1) << "\n";
        out << "End\n\n";
    }
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const auto& m = *materials[i];
        out << "Material " << (i + 1) << "\n  Name = \"" << m.name << "\"\n  Youngs Modulus = Real " << realText(m.youngModulus)
            << "\n  Poisson ratio = Real " << realText(m.poisson) << "\n  Density = Real " << realText(m.density) << "\nEnd\n\n";
    }
    for (const auto& [body, text] : bodyForces) out << "Body Force " << body << "\n" << text << "End\n\n";
    out << "Equation 1\n  Name = \"Elasticity\"\n  Active Solvers(1) = 1\nEnd\n\n";
    out << "Solver 1\n  Equation = \"Linear elasticity\"\n  Procedure = \"StressSolve\" \"StressSolver\"\n"
        << "  Variable = -dofs 3 \"Displacement\"\n  Linear System Solver = Direct\n"
        << "  Linear System Direct Method = Umfpack\n  Steady State Convergence Tolerance = 1.0e-6\nEnd\n\n";
    for (std::size_t i = 0; i < fixes.size(); ++i) {
        out << "Boundary Condition " << (fixes[i].first) << "\n  Target Boundaries(1) = " << fixes[i].first << "\n";
        for (std::size_t d = 0; d < 3; ++d)
            if (fixes[i].second[d]) out << "  Displacement " << (d + 1) << " = Real " << realText(fixValues[i][d]) << "\n";
        out << "End\n\n";
    }
    for (const auto& [tag, value] : pressures) {
        out << "Boundary Condition " << tag << "\n  Target Boundaries(1) = " << tag << "\n  Normal Force = Real "
            << realText(value) << "\nEnd\n\n";
    }
}

} // namespace

ElmerExportResult writeElmerCase(const MeshData& mesh, const AnalysisModel& scriptModel, const fs::path& directory,
                                 const std::string& jobId) {
    ElmerExportResult r;
    const AnalysisModel model = withMeshConstraints(mesh, scriptModel, r.diagnostics);
    const AnalysisJob* job = findJob(model, jobId);
    if (!job) return fail(r, jobId.empty() ? std::string("The model has no job or several jobs: name the job to write") : "No job '" + jobId + "'");
    if (!validJobName(job->id)) return fail(r, "Job name must be 1-64 characters from letters, digits, '_' and '-'");
    if (job->steps.size() != 1)
        return fail(r, "Elmer writes a job with one step for now: job '" + job->id + "' has " + std::to_string(job->steps.size()) + " steps");
    const AnalysisStep& step = job->steps.front();
    if (step.type != StepType::Static)
        return fail(r, "Elmer writes static analyses only for now: job '" + job->id + "' has a " + analysisTypeName(step.type) + " step");

    // Loads of the job's step. Those that Elmer does not write yet are refused rather than dropped silently.
    const auto active = activeLoadIds(model, step);
    auto isActive = [&](const std::string& id) { return std::find(active.begin(), active.end(), id) != active.end(); };
    for (const auto& f : model.forces)
        if (isActive(f.id)) return fail(r, "Nodal forces are not written to the Elmer case yet: use the CalculiX solver or a pressure on a surface group");
    for (const auto& d : model.distributed)
        if (isActive(d.id)) return fail(r, "Distributed loads are not written to the Elmer case yet: use the CalculiX solver");
    if (!model.rigidLinks.empty()) return fail(r, "Rigid links (rbe2) are not written to the Elmer case yet: use the CalculiX solver");
    std::vector<const SurfacePressure*> pressures;
    for (const auto& p : model.pressures) {
        if (!isActive(p.id)) continue;
        if (!p.amplitude.empty()) return fail(r, "Time-dependent loads are not written to the Elmer case yet");
        pressures.push_back(&p);
    }
    const bool gravity = model.hasGravity && isActive(kGravityLoadId);

    // Material: every material of the model must be usable; each volume block takes the material of its part.
    auto checkMaterial = [&](const AnalysisMaterial& m) -> std::string {
        if (!(m.youngModulus > 0.0) || !std::isfinite(m.youngModulus)) return "material '" + m.name + "': Young's modulus must be positive";
        if (!(m.poisson > -1.0 && m.poisson < 0.5)) return "material '" + m.name + "': Poisson's ratio must lie between -1 and 0.5 (exclusive)";
        if (gravity && !(m.density > 0.0)) return "material '" + m.name + "': gravity needs the density (rho=)";
        return {};
    };
    if (std::string e = checkMaterial(model.material); !e.empty()) return fail(r, e);
    for (const auto& m : model.materials) if (std::string e = checkMaterial(m); !e.empty()) return fail(r, e);
    if (model.supports.empty()) return fail(r, "A static analysis needs at least one support (fix ...): without it the body can move freely and the solver cannot converge");
    if (pressures.empty() && !gravity) note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_NO_LOAD", "The static analysis has no load: all results will be zero");

    // Volume blocks become bodies; the elements keep their order. Faces are kept for the pressure check.
    std::vector<VolumeElement> volumes;
    std::map<std::string, std::size_t> bodyOfBlock;   // block name -> body (first block with the name)
    std::vector<std::string> blockNames;
    std::size_t bodies = 0;
    std::map<std::vector<NodeIndex>, std::vector<std::size_t>> faceOwners;
    std::vector<std::size_t> nodeVolume(mesh.getNumNodes(), 0);
    std::vector<std::size_t> bodyOfElement;
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 3) continue;
        const int code = elmerCode(block.type);
        if (code == 0 || volumeFaces(block.type).empty())
            return fail(r, "Element type of block '" + block.name + "' has no Elmer counterpart here (use a tetrahedron, hexahedron, prism or pyramid)");
        ++bodies;
        blockNames.push_back(block.name);
        bodyOfBlock.emplace(block.name, bodies);
        const auto faces = volumeFaces(block.type);
        for (std::size_t e = 0; e < block.size(); ++e) {
            VolumeElement v;
            v.id = volumes.size() + 1;
            v.body = bodies;
            v.code = code;
            v.nodes.assign(block.connectivity.begin() + static_cast<std::ptrdiff_t>(e * block.nodesPerElement),
                           block.connectivity.begin() + static_cast<std::ptrdiff_t>((e + 1) * block.nodesPerElement));
            for (const NodeIndex n : v.nodes) if (nodeVolume[n] == 0) nodeVolume[n] = v.id;
            for (const auto& f : faces) {
                std::vector<NodeIndex> corners;
                for (const int k : f) corners.push_back(v.nodes[static_cast<std::size_t>(k)]);
                faceOwners[faceKey(corners)].push_back(v.id);
            }
            volumes.push_back(std::move(v));
        }
    }
    if (volumes.empty()) return fail(r, "The mesh has no volume elements: generate a volume mesh first (surface meshes cannot be analysed here)");
    r.bodies = bodies;

    // Material per body: the default material, or the one assigned to the block.
    std::vector<const AnalysisMaterial*> materials;
    std::vector<std::size_t> bodyMaterial(bodies, 0);
    for (const auto& pm : model.partMaterials)
        if (!bodyOfBlock.count(pm.part)) return fail(r, "assign " + pm.part + ": no volume block of the mesh has this name");
    for (std::size_t b = 0; b < bodies; ++b) {
        const AnalysisMaterial& m = materialForPart(model, blockNames[b]);
        auto found = std::find(materials.begin(), materials.end(), &m);
        if (found == materials.end()) { materials.push_back(&m); found = std::prev(materials.end()); }
        bodyMaterial[b] = static_cast<std::size_t>(std::distance(materials.begin(), found)) + 1;
    }
    note(r, ImportDiagnosticLevel::Warning, "ELMER_NODE_ORDER_UNVERIFIED",
         "Element node order is taken from the OpenModel mesh as it is; check it against the Elmer element definitions before relying on the results");

    // Fixes: a point element per constrained node, its boundary tag is the fix number.
    std::vector<BoundaryElement> boundaries;
    std::vector<std::pair<int, std::array<bool, 3>>> fixes;
    std::vector<std::array<double, 3>> fixValues;
    std::string error;
    PrescribedDisplacementTracker tracker;
    std::vector<char> inVolume(mesh.getNumNodes(), 0);
    for (const auto& v : volumes) for (const NodeIndex n : v.nodes) inVolume[n] = 1;
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        const auto& s = model.supports[i];
        if (!s.dof[0] && !s.dof[1] && !s.dof[2]) return fail(r, "fix #" + std::to_string(i + 1) + " constrains no direction");
        auto nodes = selectNodes(mesh, s.where, error);
        if (!error.empty()) return fail(r, "fix #" + std::to_string(i + 1) + ": " + error);
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](NodeIndex n) { return !inVolume[n]; }), nodes.end());
        if (nodes.empty()) return fail(r, "fix #" + std::to_string(i + 1) + ": the selection contains no node of a volume element");
        const auto conflict = tracker.add(i, s, nodes, mesh);
        if (!conflict.empty()) return fail(r, conflict);
        const int tag = static_cast<int>(i + 1);
        fixes.push_back({tag, s.dof});
        fixValues.push_back(s.displacement);
        for (const NodeIndex n : nodes) boundaries.push_back({tag, nodeVolume[n], 101, {n}});
    }

    // Pressure: each surface element of the group must be a face of exactly one volume element.
    std::unordered_map<ElementId, std::pair<const ElementBlock*, std::size_t>> surfaceLocation;
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 2) continue;
        for (std::size_t e = 0; e < block.size(); ++e) surfaceLocation.emplace(block.ids[e], std::make_pair(&block, e));
    }
    std::vector<std::pair<int, double>> pressureTags;
    std::size_t pressureIndex = 0;   // boundary tags of pressures follow those of the fixes
    for (const SurfacePressure* p : pressures) {
        const std::vector<ElementId>* groupElements = nullptr;
        int groupDimension = -1;
        if (const MeshPhysicalGroup* physical = findPhysical(mesh, p->group)) { groupElements = &physical->elementIds; groupDimension = physical->dimension; }
        else {
            const MeshGroup* generic = mesh.getMeshGroupRegistry().find(p->group);
            if (!generic)
                for (const auto& g : mesh.getMeshGroupRegistry().groups()) if (lowered(g.name) == lowered(p->group)) { generic = &g; break; }
            if (generic) { groupElements = &generic->elementIds; groupDimension = generic->dimension; }
        }
        if (!groupElements) return fail(r, "pressure: unknown surface group '" + p->group + "'");
        if (groupDimension != 2) return fail(r, "pressure: group '" + p->group + "' is not a surface group");
        if (groupElements->empty()) return fail(r, "pressure: surface group '" + p->group + "' has no surface elements");
        const int tag = static_cast<int>(model.supports.size() + ++pressureIndex);
        std::size_t unmatched = 0, shared = 0, written = 0;
        for (const ElementId id : *groupElements) {
            const auto at = surfaceLocation.find(id);
            if (at == surfaceLocation.end()) { ++unmatched; continue; }
            const ElementBlock& block = *at->second.first;
            if (block.type != ElementType::Triangle3 && block.type != ElementType::Quadrilateral4)
                return fail(r, "pressure: group '" + p->group + "' has quadratic surface elements; Elmer takes linear triangles and quadrilaterals here");
            const std::size_t k = block.nodesPerElement;
            std::vector<NodeIndex> corners(block.connectivity.begin() + static_cast<std::ptrdiff_t>(at->second.second * k),
                                           block.connectivity.begin() + static_cast<std::ptrdiff_t>((at->second.second + 1) * k));
            const auto owners = faceOwners.find(faceKey(corners));
            if (owners == faceOwners.end()) { ++unmatched; continue; }
            if (owners->second.size() != 1) { ++shared; continue; }
            boundaries.push_back({tag, owners->second.front(), block.type == ElementType::Triangle3 ? 303 : 404, corners});
            ++written;
        }
        if (unmatched) return fail(r, "pressure: " + std::to_string(unmatched) + " surface element(s) of '" + p->group + "' are not faces of a volume element");
        if (shared) return fail(r, "pressure: " + std::to_string(shared) + " surface element(s) of '" + p->group + "' lie between two volume elements; a pressure needs a boundary surface");
        pressureTags.push_back({tag, p->pressure});
        r.diagnostics.push_back({ImportDiagnosticLevel::Info, "ELMER_PRESSURE_FACES",
                                 "pressure on '" + p->group + "': " + std::to_string(written) + " face(s), boundary " + std::to_string(tag), 0});
    }
    if (!pressureTags.empty())
        note(r, ImportDiagnosticLevel::Warning, "ELMER_PRESSURE_SIGN_UNVERIFIED",
             "Pressure is written as 'Normal Force' with the sign of the script; check the direction of the load on a test case with ElmerSolver");

    r.nodes = mesh.getNumNodes();
    r.volumeElements = volumes.size();
    r.boundaryElements = boundaries.size();

    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) return fail(r, "cannot create " + directory.string() + ": " + ec.message());
    if (!writeMeshDirectory(directory, mesh, volumes, boundaries, error)) return fail(r, error);

    std::vector<std::pair<std::size_t, std::string>> bodyForces;
    if (gravity) {
        for (std::size_t b = 0; b < bodies; ++b) {
            const double rho = materials[bodyMaterial[b] - 1]->density;
            std::ostringstream text;
            text << std::setprecision(15);
            for (int d = 0; d < 3; ++d) text << "  Stress Bodyforce " << (d + 1) << " = Real " << rho * model.gravity[static_cast<std::size_t>(d)] << "\n";
            bodyForces.push_back({b + 1, text.str()});
        }
    }
    std::vector<bool> gravityFlags(bodies, gravity);
    const fs::path sifPath = directory / (job->id + ".sif");
    std::ofstream sif(sifPath);
    if (!sif) return fail(r, "cannot write " + sifPath.string());
    writeSif(sif, job->id, materials, bodyMaterial, gravityFlags, bodyForces, fixes, fixValues, pressureTags);
    sif.close();
    r.sifFile = sifPath;
    r.success = true;
    r.summary = "Elmer case: " + std::to_string(volumes.size()) + " volume elements in " + std::to_string(bodies) + " body(ies), " +
                std::to_string(boundaries.size()) + " boundary element(s), " + std::to_string(model.supports.size()) + " fix(es), " +
                std::to_string(pressureTags.size()) + " pressure(s).";
    return r;
}

std::string findElmerExecutable(const std::string& hint) {
    if (!hint.empty()) return hint;   // the user's word is final: a wrong path is reported when starting
    if (const char* env = std::getenv("OPENMODEL_ELMER"); env && *env) return env;
    const char* path = std::getenv("PATH");
    if (!path) return {};
    const char separator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    std::error_code ec;
    std::istringstream list(path);
    std::string dir;
    while (std::getline(list, dir, separator))
        if (!dir.empty() && fs::is_regular_file(fs::path(dir) / "ElmerSolver", ec)) return (fs::path(dir) / "ElmerSolver").string();
    return {};
}

std::vector<std::string> elmerLaunchCommand(const std::string& launcher, const fs::path& sifFile) {
    return {launcher, sifFile.filename().string()};
}

} // namespace OpenModel
