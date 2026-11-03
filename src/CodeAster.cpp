#include <OpenModel/CodeAster.hpp>
#include <OpenModel/AbaqusInpWriter.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifndef OPENMODEL_VERSION
#define OPENMODEL_VERSION "unknown"
#endif

namespace OpenModel {
namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t kMaxId = 9999999;   // "N" + 7 digits = 8 characters, the limit of code_aster names

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string uppered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

/** Code_aster name of an element type and the order of the OpenModel nodes in it (empty = identical). */
struct AsterType {
    const char* name = nullptr;
    std::vector<int> permutation;   ///< aster position i takes the OpenModel node permutation[i]
};

AsterType asterType(ElementType type) {
    switch (type) {
    case ElementType::Point1: return {"POI1", {}};
    case ElementType::Line2: return {"SEG2", {}};
    case ElementType::Line3: return {"SEG3", {}};
    case ElementType::Triangle3: return {"TRIA3", {}};
    case ElementType::Triangle6: return {"TRIA6", {}};
    case ElementType::Quadrilateral4: return {"QUAD4", {}};
    case ElementType::Quadrilateral8: return {"QUAD8", {}};
    case ElementType::Quadrilateral9: return {"QUAD9", {}};
    case ElementType::Tetrahedron4: return {"TETRA4", {}};
    case ElementType::Tetrahedron10: return {"TETRA10", {}};
    case ElementType::Hexahedron8: return {"HEXA8", {}};
    case ElementType::Hexahedron20: return {"HEXA20", {}};
    case ElementType::Prism6: return {"PENTA6", {}};
    // Abaqus C3D15 lists the mid-side nodes bottom, top, vertical; code_aster bottom, vertical, top.
    case ElementType::Prism15: return {"PENTA15", {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 13, 14, 9, 10, 11}};
    case ElementType::Pyramid5: return {"PYRAM5", {}};
    default: return {nullptr, {}};
    }
}

bool fromAsterType(const std::string& name, ElementType& type, std::vector<int>& permutation) {
    permutation.clear();
    static const std::pair<const char*, ElementType> table[] = {
        {"POI1", ElementType::Point1}, {"SEG2", ElementType::Line2}, {"SEG3", ElementType::Line3},
        {"TRIA3", ElementType::Triangle3}, {"TRIA6", ElementType::Triangle6}, {"QUAD4", ElementType::Quadrilateral4},
        {"QUAD8", ElementType::Quadrilateral8}, {"QUAD9", ElementType::Quadrilateral9}, {"TETRA4", ElementType::Tetrahedron4},
        {"TETRA10", ElementType::Tetrahedron10}, {"HEXA8", ElementType::Hexahedron8}, {"HEXA20", ElementType::Hexahedron20},
        {"PENTA6", ElementType::Prism6}, {"PENTA15", ElementType::Prism15}, {"PYRAM5", ElementType::Pyramid5}};
    for (const auto& entry : table)
        if (name == entry.first) {
            type = entry.second;
            if (type == ElementType::Prism15) {
                // The permutation is its own inverse only for the pairs it swaps; build the inverse explicitly.
                const std::vector<int> forward = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 13, 14, 9, 10, 11};
                permutation.assign(15, 0);
                for (int i = 0; i < 15; ++i) permutation[static_cast<std::size_t>(forward[static_cast<std::size_t>(i)])] = i;
            }
            return true;
        }
    return false;
}

/** Shortest decimal text that reads back as exactly the same double (15 to 17 significant digits). */
std::string realText(double v) {
    for (int digits = 15; digits <= 17; ++digits) {
        std::ostringstream s;
        s << std::setprecision(digits) << v;
        if (std::strtod(s.str().c_str(), nullptr) == v || digits == 17) return s.str();
    }
    return {};
}

std::string nodeName(NodeId id) { return "N" + std::to_string(id); }
std::string elementName(ElementId id) { return "M" + std::to_string(id); }

/** Group name: upper case, A-Z 0-9 and '_', at most 24 characters, not starting with a digit. */
std::string sanitizeGroup(const std::string& raw) {
    std::string s;
    for (const unsigned char c : raw) s += std::isalnum(c) ? static_cast<char>(std::toupper(c)) : '_';
    if (s.empty()) s = "GROUP";
    if (std::isdigit(static_cast<unsigned char>(s[0]))) s = "G" + s;
    if (s.size() > 24) s.resize(24);
    return s;
}

/** Hands out unique group names. */
class GroupNamer {
public:
    void reserve(const std::string& name) { used_.insert(name); }
    std::string make(const std::string& raw) {
        std::string base = sanitizeGroup(raw), name = base;
        for (int n = 2; used_.count(name); ++n) {
            const std::string suffix = "_" + std::to_string(n);
            name = base.substr(0, 24 - suffix.size()) + suffix;
        }
        used_.insert(name);
        return name;
    }
private:
    std::set<std::string> used_;
};

void putTokens(std::ostream& out, const std::string& lead, const std::vector<std::string>& tokens, std::size_t width = 76) {
    std::string line = lead;
    bool empty = true;   // nothing but the lead on the line yet
    for (const auto& t : tokens) {
        if (!empty && line.size() + 1 + t.size() > width) { out << line << "\n"; line = " "; empty = true; }
        line += (empty ? "" : " ") + t;
        empty = false;
    }
    if (!empty) out << line << "\n";
}

/** Corner faces (as node position lists) of the volume element types, used to find boundary faces. */
std::vector<std::vector<int>> cornerFaces(ElementType type) {
    switch (type) {
    case ElementType::Tetrahedron4: case ElementType::Tetrahedron10:
        return {{0, 1, 2}, {0, 1, 3}, {1, 2, 3}, {0, 2, 3}};
    case ElementType::Hexahedron8: case ElementType::Hexahedron20:
        return {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    case ElementType::Prism6: case ElementType::Prism15:
        return {{0, 1, 2}, {3, 4, 5}, {0, 1, 4, 3}, {1, 2, 5, 4}, {0, 2, 5, 3}};
    case ElementType::Pyramid5:
        return {{0, 1, 2, 3}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
    default: return {};
    }
}

int surfaceCornerCount(ElementType type) {
    switch (type) {
    case ElementType::Triangle3: case ElementType::Triangle6: return 3;
    case ElementType::Quadrilateral4: case ElementType::Quadrilateral8: case ElementType::Quadrilateral9: return 4;
    default: return 0;
    }
}

using FaceKey = std::array<NodeIndex, 4>;

FaceKey makeKey(const NodeIndex* nodes, const std::vector<int>& positions) {
    FaceKey key{{~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0)}};
    for (std::size_t i = 0; i < positions.size(); ++i) key[i] = nodes[positions[i]];
    std::sort(key.begin(), key.begin() + static_cast<std::ptrdiff_t>(positions.size()));
    return key;
}

/** Names given to the groups of a mesh file. */
struct MeshNames {
    std::vector<std::string> physical;                                  ///< per physical group of the mesh
    std::vector<std::pair<std::string, std::string>> nodeSets;          ///< (mesh name, aster name)
    std::vector<std::string> analysisNodeGroups;                        ///< aster names, in the order given
    std::vector<std::pair<std::string, std::string>> genericElementGroups; ///< (mesh name, aster name)
};

/** Node group added by the analysis (support or load selection). */
struct ExtraNodeGroup {
    std::string name;
    std::vector<NodeIndex> nodes;
};

/** Writes the mesh text. Returns an error message or an empty string. */
std::string writeMeshText(std::ostream& out, const MeshData& mesh, const std::vector<ExtraNodeGroup>& extra, bool volumeGroup,
                          MeshNames& names, std::vector<ImportDiagnostic>& diagnostics, std::size_t& volumeCount) {
    for (const NodeId id : mesh.getNodeIds())
        if (id == 0 || id > kMaxId) return "Node id " + std::to_string(id) + " cannot be used: code_aster names are 'N' + at most 7 digits (renumber the nodes from 1)";
    volumeCount = 0;
    for (const auto& block : mesh.getElementBlocks()) {
        if (!asterType(block.type).name)
            return "Element block '" + block.name + "' has a type that code_aster export does not support (27-node hexahedra and unknown types)";
        for (const ElementId id : block.ids)
            if (id == 0 || id > kMaxId) return "Element id " + std::to_string(id) + " cannot be used: code_aster names are 'M' + at most 7 digits (renumber the elements from 1)";
        if (elementDimension(block.type) == 3) volumeCount += block.size();
    }

    out << std::setprecision(17);
    out << "TITRE\nOpenModel " << OPENMODEL_VERSION << " export\nFINSF\n";
    out << "COOR_3D\n";
    for (std::size_t i = 0; i < mesh.getNumNodes(); ++i) {
        const auto c = mesh.nodeCoordinates(static_cast<NodeIndex>(i));
        out << nodeName(mesh.nodeId(static_cast<NodeIndex>(i))) << " " << realText(c[0]) << " " << realText(c[1]) << " " << realText(c[2]) << "\n";
    }
    out << "FINSF\n";

    for (const auto& block : mesh.getElementBlocks()) {
        if (block.empty()) continue;
        const AsterType at = asterType(block.type);
        out << at.name << "\n";
        for (std::size_t e = 0; e < block.size(); ++e) {
            std::vector<std::string> tokens;
            tokens.push_back(elementName(block.ids[e]));
            for (std::size_t k = 0; k < block.nodesPerElement; ++k) {
                const std::size_t source = at.permutation.empty() ? k : static_cast<std::size_t>(at.permutation[k]);
                tokens.push_back(nodeName(mesh.nodeId(block.connectivity[e * block.nodesPerElement + source])));
            }
            putTokens(out, "", tokens);
        }
        out << "FINSF\n";
    }

    GroupNamer namer;
    namer.reserve("OM_VOLUME");
    for (const auto& g : extra) namer.reserve(g.name);
    std::unordered_map<ElementId, char> present;
    for (const auto& block : mesh.getElementBlocks())
        for (const ElementId id : block.ids) present.emplace(id, 1);

    auto writeGroup = [&](const char* keyword, const std::string& name, const std::vector<std::string>& members) {
        if (members.empty()) return;
        out << keyword << " NOM = " << name << "\n";
        putTokens(out, " ", members);
        out << "FINSF\n";
    };

    std::vector<std::string> setNames;
    for (const auto& set : mesh.getNodeSets()) setNames.push_back(set.first);
    std::sort(setNames.begin(), setNames.end());
    for (const auto& setName : setNames) {
        const std::string aster = namer.make(setName);
        names.nodeSets.emplace_back(setName, aster);
        std::vector<std::string> members;
        for (const NodeIndex n : mesh.getNodeSets().at(setName)) members.push_back(nodeName(mesh.nodeId(n)));
        writeGroup("GROUP_NO", aster, members);
    }
    for (const auto& g : extra) {
        std::vector<std::string> members;
        for (const NodeIndex n : g.nodes) members.push_back(nodeName(mesh.nodeId(n)));
        writeGroup("GROUP_NO", g.name, members);
        names.analysisNodeGroups.push_back(g.name);
    }
    if (volumeGroup) {
        std::vector<std::string> members;
        for (const auto& block : mesh.getElementBlocks())
            if (elementDimension(block.type) == 3)
                for (const ElementId id : block.ids) members.push_back(elementName(id));
        writeGroup("GROUP_MA", "OM_VOLUME", members);
    }
    for (const auto& g : mesh.getPhysicalGroups()) {
        const std::string aster = namer.make(g.name);
        names.physical.push_back(aster);
        std::vector<std::string> members;
        std::size_t missing = 0;
        for (const ElementId id : g.elementIds) {
            if (present.count(id)) members.push_back(elementName(id)); else ++missing;
        }
        if (missing)
            diagnostics.push_back({ImportDiagnosticLevel::Warning, "ASTER_GROUP_MISSING_ELEMENTS",
                                   "Group '" + g.name + "' refers to " + std::to_string(missing) + " element id(s) that are not in the mesh; they were left out", 0});
        writeGroup("GROUP_MA", aster, members);
    }

    // Export the backend-independent registry as native code_aster groups too.
    // Physical groups and node sets are already emitted above; avoid duplicating
    // identical memberships while retaining distinct generic memberships.
    for (const auto& g : mesh.getMeshGroupRegistry().groups()) {
        bool representedByPhysical = false;
        for (const auto& p : mesh.getPhysicalGroups())
            if (p.name == g.name && p.dimension == g.dimension && p.elementIds == g.elementIds) {
                representedByPhysical = true;
                break;
            }
        if (!g.elementIds.empty() && !representedByPhysical) {
            std::vector<std::string> members;
            std::size_t missing = 0;
            for (const ElementId id : g.elementIds) {
                if (present.count(id)) members.push_back(elementName(id)); else ++missing;
            }
            if (missing)
                diagnostics.push_back({ImportDiagnosticLevel::Warning, "ASTER_MESH_GROUP_MISSING_ELEMENTS",
                    "Mesh group '" + g.name + "' refers to " + std::to_string(missing) +
                    " element id(s) that are not in the mesh; they were left out", 0});
            const std::string aster = namer.make(g.name);
            names.genericElementGroups.emplace_back(g.name, aster);
            writeGroup("GROUP_MA", aster, members);
        }

        bool representedByNodeSet = false;
        const auto nodeSet = mesh.getNodeSets().find(g.name + "_NODES");
        if (nodeSet != mesh.getNodeSets().end() && nodeSet->second == g.nodeIndices)
            representedByNodeSet = true;
        if (!g.nodeIndices.empty() && !representedByNodeSet) {
            std::vector<std::string> members;
            members.reserve(g.nodeIndices.size());
            for (const NodeIndex n : g.nodeIndices) members.push_back(nodeName(mesh.nodeId(n)));
            writeGroup("GROUP_NO", namer.make(g.name + "_NODES"), members);
        }
    }
    out << "FIN\n";
    return {};
}

const MeshPhysicalGroup* findGroup(const MeshData& mesh, const std::string& name) {
    for (const auto& g : mesh.getPhysicalGroups()) if (g.name == name) return &g;
    for (const auto& g : mesh.getPhysicalGroups()) if (lowered(g.name) == lowered(name)) return &g;
    return nullptr;
}

std::string pyReal(double v) {
    std::string t = realText(v);
    if (t.find_first_of(".eEn") == std::string::npos) t += ".0";   // keep a float literal for the Python command file
    return t;
}

bool validJobName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (const unsigned char c : name) if (!std::isalnum(c) && c != '_' && c != '-') return false;
    return true;
}

bool knownSolver(const std::string& s) { return s == "MULT_FRONT" || s == "MUMPS" || s == "PETSC" || s == "LDLT"; }

CodeAsterExportResult fail(CodeAsterExportResult r, std::string message) {
    r.success = false;
    r.error = std::move(message);
    return r;
}

void note(CodeAsterExportResult& r, ImportDiagnosticLevel level, std::string code, std::string message) {
    r.diagnostics.push_back({level, std::move(code), std::move(message), 0});
}

} // namespace

/* --------------------------------------------------------------------------- mesh export */

std::string writeCodeAsterMesh(const fs::path& path, const MeshData& mesh) {
    std::ostringstream text;
    MeshNames names;
    std::vector<ImportDiagnostic> diagnostics;
    std::size_t volumes = 0;
    const std::string error = writeMeshText(text, mesh, {}, false, names, diagnostics, volumes);
    if (!error.empty()) return error;
    std::ofstream file(path, std::ios::binary);
    if (!file) return "Cannot write " + path.string();
    const std::string data = text.str();
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();
    return file ? std::string() : "Failed while writing " + path.string();
}

/* --------------------------------------------------------------------------- case export */

CodeAsterExportResult writeCodeAsterCase(const MeshData& mesh, const AnalysisModel& model, const fs::path& directory,
                                         const CodeAsterOptions& options) {
    CodeAsterExportResult r;
    if (!validJobName(options.jobName)) return fail(r, "Job name must be 1-64 characters from letters, digits, '_' and '-'");
    if (!knownSolver(options.solver)) return fail(r, "Unknown linear solver '" + options.solver + "' (MULT_FRONT, MUMPS, PETSC, LDLT)");
    if (options.timeLimitSeconds < 1 || options.memoryLimitMB < 1 || options.ncpus < 1) return fail(r, "time limit, memory limit and number of processors must be positive");

    const auto& m = model.material;
    if (!(m.youngModulus > 0.0) || !std::isfinite(m.youngModulus)) return fail(r, "Young's modulus must be positive");
    if (!(m.poisson > -1.0 && m.poisson < 0.5)) return fail(r, "Poisson's ratio must lie between -1 and 0.5 (exclusive)");
    const bool frequency = model.step == StepType::Frequency;
    if ((frequency || model.hasGravity) && !(m.density > 0.0)) return fail(r, "A frequency analysis and gravity need the density (rho=)");
    if (model.hasGravity && frequency) note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_GRAVITY_IGNORED", "Loads (gravity included) have no effect in a frequency analysis");
    if (frequency && (!model.forces.empty() || !model.pressures.empty()))
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_LOADS_IGNORED", "Forces and pressures have no effect in a frequency analysis");
    if (!frequency && model.supports.empty())
        return fail(r, "A static analysis needs at least one support (fix ...): without it the body can move freely and the solver cannot converge");
    if (!frequency && model.forces.empty() && model.pressures.empty() && !model.hasGravity)
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_NO_LOAD", "The static analysis has no load: all results will be zero");
    if (frequency && model.supports.empty())
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_FREE_BODY", "Frequency analysis without supports: the first modes are rigid body motions with zero frequency");

    // Volume elements, their nodes, and a face map for the pressure check.
    std::vector<char> inVolume(mesh.getNumNodes(), 0);
    std::map<FaceKey, std::size_t> faceCount;
    std::size_t volumeCount = 0;
    std::size_t negative = 0;
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 3) continue;
        const auto faces = cornerFaces(block.type);
        for (std::size_t e = 0; e < block.size(); ++e) {
            const NodeIndex* nodes = &block.connectivity[e * block.nodesPerElement];
            for (std::size_t k = 0; k < block.nodesPerElement; ++k) inVolume[nodes[k]] = 1;
            for (const auto& f : faces) ++faceCount[makeKey(nodes, f)];
            if (block.type == ElementType::Tetrahedron4 || block.type == ElementType::Tetrahedron10) {
                const auto a = mesh.nodeCoordinates(nodes[0]), b = mesh.nodeCoordinates(nodes[1]), c = mesh.nodeCoordinates(nodes[2]), d = mesh.nodeCoordinates(nodes[3]);
                const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]}, w[3] = {d[0] - a[0], d[1] - a[1], d[2] - a[2]};
                const double det = u[0] * (v[1] * w[2] - v[2] * w[1]) - u[1] * (v[0] * w[2] - v[2] * w[0]) + u[2] * (v[0] * w[1] - v[1] * w[0]);
                if (det < 0.0) ++negative;
            }
        }
        volumeCount += block.size();
    }
    if (volumeCount == 0) return fail(r, "The mesh has no volume elements: generate a volume mesh first (surface meshes cannot be analysed here)");
    if (negative)
        note(r, ImportDiagnosticLevel::Warning, "ASTER_NEGATIVE_TETRA",
             std::to_string(negative) + " tetrahedra have negative signed volume with the OpenModel (Abaqus) node order; code_aster may refuse them (check with the mesh checks of code_aster or reorder the nodes)");

    // Selections, restricted to nodes of volume elements.
    std::string error;
    std::vector<ExtraNodeGroup> extra;
    std::vector<std::size_t> supportGroup, forceGroup;
    std::vector<std::vector<NodeIndex>> forceNodes;
    std::vector<char> constrained(mesh.getNumNodes(), 0);
    auto restrict = [&](std::vector<NodeIndex> nodes, const std::string& what) -> std::vector<NodeIndex> {
        const std::size_t before = nodes.size();
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](NodeIndex n) { return !inVolume[n]; }), nodes.end());
        if (nodes.size() != before)
            note(r, ImportDiagnosticLevel::Warning, "ASTER_SELECTION_OUTSIDE_VOLUME",
                 what + ": " + std::to_string(before - nodes.size()) + " selected node(s) belong to no volume element and were left out");
        return nodes;
    };
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        const auto& s = model.supports[i];
        if (!s.dof[0] && !s.dof[1] && !s.dof[2]) return fail(r, "fix #" + std::to_string(i + 1) + " constrains no direction");
        auto nodes = selectNodes(mesh, s.where, error);
        if (!error.empty()) return fail(r, "fix #" + std::to_string(i + 1) + ": " + error);
        nodes = restrict(std::move(nodes), "fix #" + std::to_string(i + 1));
        if (nodes.empty()) return fail(r, "fix #" + std::to_string(i + 1) + ": the selection contains no node of a volume element");
        for (const NodeIndex n : nodes) {
          for (int d = 0; d < 3; ++d) {
            const double value = s.displacement[static_cast<std::size_t>(d)];
            if (s.dof[static_cast<std::size_t>(d)] && !std::isfinite(value))
                return fail(r, "fix #" + std::to_string(i + 1) + " has a non-finite prescribed displacement");
            if (!s.dof[static_cast<std::size_t>(d)]) continue;
            for (std::size_t previous = 0; previous < i; ++previous) {
                if (!model.supports[previous].dof[static_cast<std::size_t>(d)]) continue;
                const auto& previousNodes = extra[supportGroup[previous]].nodes;
                if (std::find(previousNodes.begin(), previousNodes.end(), n) == previousNodes.end()) continue;
                const double other = model.supports[previous].displacement[static_cast<std::size_t>(d)];
                const double scale = std::max({1.0, std::fabs(value), std::fabs(other)});
                if (std::fabs(value - other) > 1e-12 * scale)
                    return fail(r, "conflicting prescribed displacements: fix #" + std::to_string(previous + 1) +
                                   " and fix #" + std::to_string(i + 1) + " assign different values to node " +
                                   std::to_string(mesh.nodeId(n)) + " degree of freedom " + std::to_string(d + 1));
            }
          }
        }
        for (const NodeIndex n : nodes) constrained[n] = 1;
        supportGroup.push_back(extra.size());
        extra.push_back({"OM_FIX" + std::to_string(i + 1), std::move(nodes)});
    }
    for (const char c : constrained) r.fixedNodes += c ? 1 : 0;
    for (std::size_t i = 0; i < model.forces.size(); ++i) {
        auto nodes = selectNodes(mesh, model.forces[i].where, error);
        if (!error.empty()) return fail(r, "force #" + std::to_string(i + 1) + ": " + error);
        nodes = restrict(std::move(nodes), "force #" + std::to_string(i + 1));
        if (nodes.empty()) return fail(r, "force #" + std::to_string(i + 1) + ": the selection contains no node of a volume element");
        r.loadedNodes += nodes.size();
        forceGroup.push_back(extra.size());
        forceNodes.push_back(nodes);
        extra.push_back({"OM_LOAD" + std::to_string(i + 1), std::move(nodes)});
    }
    bool quadratic = false;
    for (const auto& b : mesh.getElementBlocks()) quadratic = quadratic || b.type == ElementType::Tetrahedron10 || b.type == ElementType::Hexahedron20 || b.type == ElementType::Prism15;
    if (quadratic && !model.forces.empty())
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_FORCE_QUADRATIC",
             "A force shared equally by all nodes of a quadratic mesh is not the consistent load; use 'pressure' on a surface group for accurate results");

    // Pressure: every surface element of the group must be a boundary face of exactly one volume element.
    std::vector<std::string> pressureGroups;
    {
        std::unordered_map<ElementId, std::pair<const ElementBlock*, std::size_t>> location;
        if (!model.pressures.empty())
            for (const auto& block : mesh.getElementBlocks())
                for (std::size_t e = 0; e < block.size(); ++e) location.emplace(block.ids[e], std::make_pair(&block, e));
        for (const auto& p : model.pressures) {
            const std::vector<ElementId>* groupElements = nullptr;
            int groupDimension = -1;
            const MeshPhysicalGroup* physical = findGroup(mesh, p.group);
            if (physical) { groupElements = &physical->elementIds; groupDimension = physical->dimension; }
            else {
                const MeshGroup* generic = mesh.getMeshGroupRegistry().find(p.group);
                if (!generic)
                    for (const auto& g : mesh.getMeshGroupRegistry().groups())
                        if (lowered(g.name) == lowered(p.group)) { generic = &g; break; }
                if (generic) { groupElements = &generic->elementIds; groupDimension = generic->dimension; }
            }
            if (!groupElements) return fail(r, "pressure: unknown surface group '" + p.group + "'");
            if (groupDimension != 2) return fail(r, "pressure: group '" + p.group + "' is not a surface group (dimension " + std::to_string(groupDimension) + ")");
            if (groupElements->empty()) return fail(r, "pressure: surface group '" + p.group + "' has no surface elements");
            std::size_t unmatched = 0, shared = 0;
            for (const ElementId id : *groupElements) {
                const auto at = location.find(id);
                const int corners = at == location.end() ? 0 : surfaceCornerCount(at->second.first->type);
                if (corners == 0) { ++unmatched; continue; }
                const auto& block = *at->second.first;
                std::vector<int> positions(static_cast<std::size_t>(corners));
                for (int k = 0; k < corners; ++k) positions[static_cast<std::size_t>(k)] = k;
                const auto hit = faceCount.find(makeKey(&block.connectivity[at->second.second * block.nodesPerElement], positions));
                if (hit == faceCount.end()) ++unmatched; else if (hit->second != 1) ++shared; else ++r.pressureFaces;
            }
            if (unmatched) return fail(r, "pressure: " + std::to_string(unmatched) + " surface element(s) of '" + p.group + "' are not faces of a volume element");
            if (shared) return fail(r, "pressure: " + std::to_string(shared) + " surface element(s) of '" + p.group + "' lie between two volume elements; a pressure needs a boundary surface");
            pressureGroups.push_back(p.group);
        }
    }

    // Files.
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) return fail(r, "Cannot create " + directory.string() + ": " + ec.message());
    const fs::path absolute = fs::absolute(directory);
    const std::string job = options.jobName;
    r.meshFile = absolute / (job + ".mail");
    r.commFile = absolute / (job + ".comm");
    r.exportFile = absolute / (job + ".export");

    MeshNames names;
    {
        std::ostringstream text;
        std::size_t volumes = 0;
        const std::string meshError = writeMeshText(text, mesh, extra, true, names, r.diagnostics, volumes);
        if (!meshError.empty()) return fail(r, meshError);
        std::ofstream file(r.meshFile, std::ios::binary);
        if (!file) return fail(r, "Cannot write " + r.meshFile.string());
        const std::string data = text.str();
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file) return fail(r, "Failed while writing " + r.meshFile.string());
        r.volumeElements = volumes;
    }
    r.nodes = mesh.getNumNodes();
    r.elements = mesh.getNumElements();

    // Group names of the pressure surfaces, in the order of model.pressures.
    std::vector<std::string> pressureNames;
    for (const auto& rawName : pressureGroups) {
        bool found = false;
        const auto& physical = mesh.getPhysicalGroups();
        for (std::size_t i = 0; i < physical.size(); ++i)
            if (physical[i].name == rawName || lowered(physical[i].name) == lowered(rawName)) {
                pressureNames.push_back(names.physical.at(i));
                found = true;
                break;
            }
        if (!found) {
            for (const auto& entry : names.genericElementGroups)
                if (entry.first == rawName || lowered(entry.first) == lowered(rawName)) {
                    pressureNames.push_back(entry.second);
                    found = true;
                    break;
                }
        }
        if (!found) return fail(r, "pressure: no exported element group for surface '" + rawName + "'");
    }

    std::ostringstream c;
    c << "# -*- coding: utf-8 -*-\n"
      << "# Generated by OpenModel " << OPENMODEL_VERSION << ": " << (frequency ? "free vibration" : "linear static") << " analysis.\n"
      << "# Units are those of the mesh; E, NU, RHO, loads and results use the same consistent set.\n"
      << "DEBUT(LANG='EN')\n\n"
      << "mesh = LIRE_MAILLAGE(FORMAT='ASTER', UNITE=20)\n\n"
      << "model = AFFE_MODELE(AFFE=_F(GROUP_MA='OM_VOLUME', MODELISATION='3D', PHENOMENE='MECANIQUE'), MAILLAGE=mesh)\n\n"
      << "steel = DEFI_MATERIAU(ELAS=_F(E=" << pyReal(m.youngModulus) << ", NU=" << pyReal(m.poisson);
    if (m.density > 0.0) c << ", RHO=" << pyReal(m.density);
    c << "))\n\n"
      << "chmat = AFFE_MATERIAU(AFFE=_F(GROUP_MA='OM_VOLUME', MATER=steel), MAILLAGE=mesh)\n\n";

    // Supports.
    c << "support = AFFE_CHAR_MECA(MODELE=model, DDL_IMPO=(\n";
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        c << "    _F(GROUP_NO='" << extra[supportGroup[i]].name << "'";
        const char* dof[3] = {"DX", "DY", "DZ"};
        for (int d = 0; d < 3; ++d) if (model.supports[i].dof[static_cast<std::size_t>(d)])
            c << ", " << dof[d] << "=" << pyReal(model.supports[i].displacement[static_cast<std::size_t>(d)]);
        c << "),\n";
    }
    c << "))\n\n";
    const bool anySupport = !model.supports.empty();
    if (!anySupport) {   // frequency analysis without supports: no condition at all
        c.str(std::string());
        c.clear();
        c << "# -*- coding: utf-8 -*-\n"
          << "# Generated by OpenModel " << OPENMODEL_VERSION << ": free vibration analysis without supports.\n"
          << "DEBUT(LANG='EN')\n\nmesh = LIRE_MAILLAGE(FORMAT='ASTER', UNITE=20)\n\n"
          << "model = AFFE_MODELE(AFFE=_F(GROUP_MA='OM_VOLUME', MODELISATION='3D', PHENOMENE='MECANIQUE'), MAILLAGE=mesh)\n\n"
          << "steel = DEFI_MATERIAU(ELAS=_F(E=" << pyReal(m.youngModulus) << ", NU=" << pyReal(m.poisson) << ", RHO=" << pyReal(m.density) << "))\n\n"
          << "chmat = AFFE_MATERIAU(AFFE=_F(GROUP_MA='OM_VOLUME', MATER=steel), MAILLAGE=mesh)\n\n";
    }

    auto tableDump = [&](const std::string& variable, const std::string& resultName, const char* field, const char* label, int unit) {
        c << variable << " = CREA_TABLE(RESU=_F(RESULTAT=" << resultName << ", NOM_CHAM='" << field << "', TOUT='OUI', TOUT_CMP='OUI'))\n"
          << "IMPR_TABLE(TABLE=" << variable << ", FORMAT='TABLEAU', UNITE=" << unit << ", SEPARATEUR=',', FORMAT_R='1PE16.9')   # " << label << "\n";
    };

    std::vector<std::pair<std::string, int>> outputs;   // file name, unit
    if (!frequency) {
        std::vector<std::string> loads;
        if (anySupport) loads.push_back("support");
        const bool hasForce = !model.forces.empty(), hasPressure = !model.pressures.empty();
        if (hasForce) {
            c << "forces = AFFE_CHAR_MECA(MODELE=model, FORCE_NODALE=(\n";
            for (std::size_t i = 0; i < model.forces.size(); ++i) {
                const double count = static_cast<double>(forceNodes[i].size());
                c << "    _F(GROUP_NO='" << extra[forceGroup[i]].name << "'";
                const char* comp[3] = {"FX", "FY", "FZ"};
                for (int d = 0; d < 3; ++d) {
                    const double share = model.forces[i].total[static_cast<std::size_t>(d)] / count;
                    if (share != 0.0) c << ", " << comp[d] << "=" << pyReal(share);
                }
                c << "),\n";
            }
            c << "))\n\n";
            loads.push_back("forces");
        }
        if (hasPressure) {
            c << "pressure = AFFE_CHAR_MECA(MODELE=model, PRES_REP=(\n";
            for (std::size_t i = 0; i < model.pressures.size(); ++i)
                c << "    _F(GROUP_MA='" << pressureNames[i] << "', PRES=" << pyReal(model.pressures[i].pressure) << "),\n";
            c << "))\n\n";
            loads.push_back("pressure");
        }
        if (model.hasGravity) {
            const auto& g = model.gravity;
            const double magnitude = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
            if (magnitude > 0.0) {
                c << "weight = AFFE_CHAR_MECA(MODELE=model, PESANTEUR=_F(GRAVITE=" << pyReal(magnitude) << ", DIRECTION=("
                  << pyReal(g[0] / magnitude) << ", " << pyReal(g[1] / magnitude) << ", " << pyReal(g[2] / magnitude) << "), GROUP_MA='OM_VOLUME'))\n\n";
                loads.push_back("weight");
            }
        }
        c << "resu = MECA_STATIQUE(MODELE=model, CHAM_MATER=chmat, SOLVEUR=_F(METHODE='" << options.solver << "'), EXCIT=(\n";
        for (const auto& l : loads) c << "    _F(CHARGE=" << l << "),\n";
        c << "))\n\n"
          << "resu = CALC_CHAMP(reuse=resu, RESULTAT=resu, CONTRAINTE=('SIGM_ELNO', 'SIGM_NOEU'), DEFORMATION=('EPSI_ELNO', 'EPSI_NOEU'))\n\n"
          << "# Nodal results as text tables read back by OpenModel (node names are N<id>).\n";
        tableDump("tdisp", "resu", "DEPL", "displacements", 80);
        tableDump("tsigm", "resu", "SIGM_NOEU", "stresses", 81);
        tableDump("teps", "resu", "EPSI_NOEU", "strains", 82);
        outputs = {{job + "_disp.tab", 80}, {job + "_stress.tab", 81}, {job + "_strain.tab", 82}};
    } else {
        c << "rigi = CALC_MATR_ELEM(MODELE=model, CHAM_MATER=chmat" << (anySupport ? ", CHARGE=support" : "") << ", OPTION='RIGI_MECA')\n"
          << "mass = CALC_MATR_ELEM(MODELE=model, CHAM_MATER=chmat" << (anySupport ? ", CHARGE=support" : "") << ", OPTION='MASS_MECA')\n"
          << "nume = NUME_DDL(MATR_RIGI=rigi)\n"
          << "matk = ASSE_MATRICE(MATR_ELEM=rigi, NUME_DDL=nume)\n"
          << "matm = ASSE_MATRICE(MATR_ELEM=mass, NUME_DDL=nume)\n\n"
          << "modes = CALC_MODES(MATR_RIGI=matk, MATR_MASS=matm, OPTION='PLUS_PETITE', CALC_FREQ=_F(NMAX_FREQ=" << model.modes << "))\n\n"
          << "# Mode shapes and frequencies as text tables read back by OpenModel.\n";
        tableDump("tmode", "modes", "DEPL", "mode shapes", 80);
        c << "\n# Frequencies (Hz when the unit of time is the second). A failure here does not stop the run.\n"
          << "try:\n"
          << "    _par = modes.getAccessParameters()\n"
          << "    _orders = list(_par['NUME_ORDRE'])\n"
          << "    _freqs = list(_par['FREQ'])\n"
          << "    with open('fort.81', 'w') as _f:\n"
          << "        _f.write('NUME_ORDRE,FREQ\\n')\n"
          << "        for _o, _q in zip(_orders, _freqs):\n"
          << "            _f.write('%d,%.12e\\n' % (_o, _q))\n"
          << "except Exception as _exc:\n"
          << "    print('OpenModel: could not write the frequency table: %s' % (_exc,))\n";
        outputs = {{job + "_modes_disp.tab", 80}, {job + "_modes.tab", 81}};
    }
    c << "\nFIN()\n";

    {
        std::ofstream file(r.commFile, std::ios::binary);
        if (!file) return fail(r, "Cannot write " + r.commFile.string());
        const std::string data = c.str();
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file) return fail(r, "Failed while writing " + r.commFile.string());
    }

    {
        std::ostringstream x;
        x << "P actions make_etude\n"
          << "P mode interactif\n"
          << "P ncpus " << options.ncpus << "\n"
          << "P memory_limit " << options.memoryLimitMB << "\n"
          << "P time_limit " << options.timeLimitSeconds << "\n";
        if (!options.version.empty()) x << "P version " << options.version << "\n";
        x << "F comm " << r.commFile.string() << " D 1\n"
          << "F mail " << r.meshFile.string() << " D 20\n"
          << "F mess " << (absolute / (job + ".mess")).string() << " R 6\n";
        for (const auto& o : outputs) x << "F libr " << (absolute / o.first).string() << " R " << o.second << "\n";
        std::ofstream file(r.exportFile, std::ios::binary);
        if (!file) return fail(r, "Cannot write " + r.exportFile.string());
        const std::string data = x.str();
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file) return fail(r, "Failed while writing " + r.exportFile.string());
    }

    std::ostringstream summary;
    summary << (frequency ? "code_aster frequency analysis (" + std::to_string(model.modes) + " modes)" : std::string("code_aster static analysis"))
            << " of " << r.volumeElements << " volume elements, " << r.nodes << " nodes; E = " << m.youngModulus << ", nu = " << m.poisson;
    if (r.fixedNodes) summary << "; " << r.fixedNodes << " constrained nodes";
    if (r.loadedNodes) summary << "; force on " << r.loadedNodes << " nodes";
    if (r.pressureFaces) summary << "; pressure on " << r.pressureFaces << " faces";
    if (model.hasGravity) summary << "; gravity";
    r.summary = summary.str();
    r.success = true;
    return r;
}

/* --------------------------------------------------------------------------- run helpers */

std::string findCodeAsterExecutable(const std::string& hint) {
    std::error_code ec;
    auto usable = [&](const fs::path& p) { return fs::is_regular_file(p, ec); };
    if (!hint.empty()) return hint;   // the user's word is final: a wrong path is reported when starting
    if (const char* env = std::getenv("OPENMODEL_CODE_ASTER"); env && *env) return env;
    const char* path = std::getenv("PATH");
    if (!path) return {};
    const char separator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    for (const char* name : {"run_aster", "as_run"}) {
        std::istringstream list(path);
        std::string dir;
        while (std::getline(list, dir, separator))
            if (!dir.empty() && usable(fs::path(dir) / name)) return (fs::path(dir) / name).string();
    }
    return {};
}

std::vector<std::string> codeAsterLaunchCommand(const std::string& launcher, const fs::path& exportFile) {
    return {launcher, exportFile.filename().string()};
}

std::vector<fs::path> codeAsterResultFiles(const fs::path& directory, const std::string& jobName) {
    std::vector<fs::path> files;
    std::error_code ec;
    // The eigenvalue table goes first: it carries the frequencies of the modes that the mode shapes are merged into.
    for (const char* suffix : {"_modes", "_modes_disp", "_disp", "_stress", "_strain"}) {
        const fs::path p = directory / (jobName + suffix + ".tab");
        if (fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 0) files.push_back(p);
    }
    return files;
}

void removeCodeAsterOutputs(const fs::path& directory, const std::string& jobName) {
    std::error_code ec;
    for (const char* suffix : {"_disp.tab", "_stress.tab", "_strain.tab", "_modes_disp.tab", "_modes.tab", ".mess", ".resu"})
        fs::remove(directory / (jobName + suffix), ec);
}

/* --------------------------------------------------------------------------- mesh import */

std::string CodeAsterMeshImporter::formatName() const { return "code_aster mesh (ASTER)"; }

bool CodeAsterMeshImporter::canRead(const fs::path& path) const { return lowered(path.extension().string()) == ".mail"; }

namespace {

/** Digits at the end of a name such as N12 / M7 / NO12; false when there are none or the name has digits in the middle. */
bool trailingNumber(const std::string& name, std::uint64_t& value) {
    std::size_t i = name.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) --i;
    if (i == name.size() || name.size() - i > 18) return false;
    for (std::size_t k = 0; k < i; ++k) if (!std::isalpha(static_cast<unsigned char>(name[k])) && name[k] != '_') return false;
    value = std::stoull(name.substr(i));
    return true;
}

struct RawElement {
    std::string name;
    ElementType type;
    std::vector<std::string> nodes;
    std::string blockName;
};

std::vector<std::string> tokensOf(const std::string& line) {
    std::vector<std::string> t;
    std::istringstream in(line);
    for (std::string s; in >> s;) t.push_back(s);
    return t;
}

bool realOf(const std::string& s, double& v) {
    char* end = nullptr;
    // Fortran style exponents (1.0D+02) are accepted.
    std::string text = s;
    for (char& ch : text) if (ch == 'D' || ch == 'd') ch = 'E';
    v = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(v);
}

} // namespace

MeshImportResult CodeAsterMeshImporter::read(const fs::path& path, MeshData& destination, const MeshImportOptions& options) const {
    MeshImportResult result;
    std::ifstream in(path);
    if (!in) { result.addError(ImportStatus::IoError, "IO_OPEN", "Cannot open " + path.string()); return result; }

    struct NodeRow { std::string name; std::array<double, 3> c; };
    std::vector<NodeRow> nodes;
    std::vector<RawElement> elements;
    std::vector<std::pair<std::string, std::vector<std::string>>> nodeGroups, elementGroups;

    enum class Section { None, Coor, Elements, GroupNo, GroupMa, Skip } section = Section::None;
    std::string blockName;
    ElementType blockType = ElementType::Unknown;
    std::vector<int> blockPermutation;
    std::vector<std::string> pending;   // tokens of an element block not yet consumed
    std::string groupName;
    std::vector<std::string> groupMembers;
    bool coor2d = false, finished = false;
    std::size_t lineNumber = 0;
    std::set<std::string> warnedBlocks;

    auto flushElements = [&]() {
        const std::size_t count = elementNodeCount(blockType);
        std::size_t pos = 0;
        while (pos + 1 + count <= pending.size()) {
            RawElement e;
            e.name = pending[pos];
            e.type = blockType;
            e.blockName = blockName;
            for (std::size_t k = 0; k < count; ++k) e.nodes.push_back(pending[pos + 1 + k]);
            if (!blockPermutation.empty()) {
                std::vector<std::string> reordered(count);
                for (std::size_t k = 0; k < count; ++k) reordered[static_cast<std::size_t>(blockPermutation[k])] = e.nodes[k];
                e.nodes = std::move(reordered);
            }
            elements.push_back(std::move(e));
            pos += 1 + count;
        }
        if (pos != pending.size())
            result.addWarning("ASTER_BLOCK_TRUNCATED", "Block " + blockName + " ends with an incomplete element; it was ignored", lineNumber);
        pending.clear();
    };

    std::string line;
    while (!finished && std::getline(in, line)) {
        ++lineNumber;
        if (const auto pct = line.find('%'); pct != std::string::npos) line.erase(pct);   // comment
        auto tokens = tokensOf(line);
        if (tokens.empty()) continue;
        const std::string head = uppered(tokens[0]);

        if (head == "FIN") { finished = true; break; }
        if (head == "FINSF") {
            if (section == Section::Elements) flushElements();
            else if (section == Section::GroupNo) nodeGroups.emplace_back(groupName, groupMembers);
            else if (section == Section::GroupMa) elementGroups.emplace_back(groupName, groupMembers);
            section = Section::None;
            groupMembers.clear();
            continue;
        }
        if (section == Section::None) {
            if (head == "COOR_3D" || head == "COOR_2D") { section = Section::Coor; coor2d = head == "COOR_2D"; continue; }
            if (head == "GROUP_NO" || head == "GROUP_MA") {
                section = head == "GROUP_NO" ? Section::GroupNo : Section::GroupMa;
                groupName.clear();
                groupMembers.clear();
                // "GROUP_NO NOM = name" ; the name may also follow on the same line after the keyword.
                std::vector<std::string> rest(tokens.begin() + 1, tokens.end());
                std::vector<std::string> filtered;
                for (const auto& t : rest) {
                    const std::string u = uppered(t);
                    if (u == "NOM" || u == "=") continue;
                    if (u.rfind("NOM=", 0) == 0) { if (t.size() > 4) filtered.push_back(t.substr(4)); continue; }
                    filtered.push_back(t);
                }
                if (!filtered.empty()) { groupName = filtered[0]; groupMembers.assign(filtered.begin() + 1, filtered.end()); }
                continue;
            }
            ElementType type;
            std::vector<int> permutation;
            if (fromAsterType(head, type, permutation)) {
                section = Section::Elements;
                blockName = head;
                blockType = type;
                blockPermutation = permutation;
                pending.clear();
                for (std::size_t i = 1; i < tokens.size(); ++i) pending.push_back(tokens[i]);
                continue;
            }
            if (head == "TITRE" || head == "TITRE:" ) { section = Section::Skip; continue; }
            // Anything else is an unknown block: skip until FINSF.
            if (warnedBlocks.insert(head).second)
                result.addWarning("ASTER_UNKNOWN_BLOCK", "Block '" + tokens[0] + "' is not supported and was skipped", lineNumber);
            section = Section::Skip;
            continue;
        }
        switch (section) {
        case Section::Coor: {
            double x = 0, y = 0, z = 0;
            const std::size_t need = coor2d ? 3 : 4;
            if (tokens.size() < need || !realOf(tokens[1], x) || !realOf(tokens[2], y) || (!coor2d && !realOf(tokens[3], z))) {
                result.addError(ImportStatus::InvalidInput, "ASTER_BAD_COORDINATES", "Malformed node line: " + line, lineNumber);
                return result;
            }
            nodes.push_back({tokens[0], {x, y, z}});
            break;
        }
        case Section::Elements:
            for (const auto& t : tokens) pending.push_back(t);
            break;
        case Section::GroupNo: case Section::GroupMa:
            for (const auto& t : tokens) {
                if (groupName.empty()) {
                    const std::string u = uppered(t);
                    if (u == "NOM" || u == "=") continue;
                    groupName = t;
                } else groupMembers.push_back(t);
            }
            break;
        default: break;
        }
    }
    if (nodes.empty()) { result.addError(ImportStatus::InvalidInput, "ASTER_NO_NODES", "The file has no COOR_3D block with nodes (is it a MED file? MED is not supported)"); return result; }
    if (elements.empty()) { result.addError(ImportStatus::InvalidInput, "ASTER_NO_ELEMENTS", "The file has no supported element block"); return result; }
    if (!finished) result.addWarning("ASTER_NO_FIN", "The file does not end with FIN");

    // Ids: the number in the name when every name has one and they are unique, otherwise 1..n.
    auto assign = [&](const std::vector<std::string>& names, const char* what) {
        std::vector<std::uint64_t> ids(names.size());
        std::set<std::uint64_t> seen;
        bool numeric = true;
        for (std::size_t i = 0; i < names.size() && numeric; ++i) {
            std::uint64_t v = 0;
            if (!trailingNumber(names[i], v) || v == 0 || !seen.insert(v).second) numeric = false;
            else ids[i] = v;
        }
        if (!numeric) {
            for (std::size_t i = 0; i < names.size(); ++i) ids[i] = i + 1;
            result.addWarning("ASTER_IDS_RENUMBERED", std::string(what) + " names carry no unique number: they are numbered from 1 in file order");
        }
        return ids;
    };
    std::vector<std::string> nodeNames, elementNames;
    for (const auto& n : nodes) nodeNames.push_back(n.name);
    for (const auto& e : elements) elementNames.push_back(e.name);
    const auto nodeIds = assign(nodeNames, "Node");
    const auto elementIds = assign(elementNames, "Element");

    MeshData mesh;
    std::unordered_map<std::string, NodeIndex> nodeIndex;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodeIndex.count(nodes[i].name)) { result.addError(ImportStatus::InvalidInput, "ASTER_DUPLICATE_NODE", "Node name '" + nodes[i].name + "' is defined twice"); return result; }
        nodeIndex[nodes[i].name] = mesh.addNode(nodeIds[i], nodes[i].c[0], nodes[i].c[1], nodes[i].c[2]);
    }
    std::unordered_map<std::string, ElementId> elementIdOf;
    std::unordered_map<ElementId, ElementType> elementTypeOf;
    for (std::size_t i = 0; i < elements.size(); ++i) {
        std::vector<NodeIndex> conn;
        for (const auto& name : elements[i].nodes) {
            const auto it = nodeIndex.find(name);
            if (it == nodeIndex.end()) {
                result.addError(ImportStatus::InvalidInput, "ASTER_UNKNOWN_NODE", "Element '" + elements[i].name + "' uses the undefined node '" + name + "'");
                return result;
            }
            conn.push_back(it->second);
        }
        if (elementIdOf.count(elements[i].name)) { result.addError(ImportStatus::InvalidInput, "ASTER_DUPLICATE_ELEMENT", "Element name '" + elements[i].name + "' is defined twice"); return result; }
        elementIdOf[elements[i].name] = elementIds[i];
        elementTypeOf[elementIds[i]] = elements[i].type;
        mesh.addElement(elements[i].blockName, elements[i].type, elementIds[i], conn);
    }
    result.nodesRead = mesh.getNumNodes();
    result.elementsRead = mesh.getNumElements();

    if (options.importNodeSets)
        for (const auto& g : nodeGroups) {
            std::vector<NodeIndex> members;
            for (const auto& name : g.second) {
                const auto it = nodeIndex.find(name);
                if (it == nodeIndex.end()) { result.addWarning("ASTER_GROUP_UNKNOWN_NODE", "GROUP_NO " + g.first + ": unknown node '" + name + "' ignored"); continue; }
                members.push_back(it->second);
            }
            if (members.empty()) continue;
            mesh.addNodeSet(g.first, members);
            ++result.nodeSetsRead;
        }
    if (options.importPhysicalGroups || options.importElementSets) {
        int tag = 1;
        std::set<std::string> used;
        for (const auto& g : elementGroups) {
            std::map<int, std::vector<ElementId>> byDimension;
            for (const auto& name : g.second) {
                const auto it = elementIdOf.find(name);
                if (it == elementIdOf.end()) { result.addWarning("ASTER_GROUP_UNKNOWN_ELEMENT", "GROUP_MA " + g.first + ": unknown element '" + name + "' ignored"); continue; }
                byDimension[elementDimension(elementTypeOf[it->second])].push_back(it->second);
            }
            for (const auto& entry : byDimension) {
                std::string name = g.first;
                if (byDimension.size() > 1) name += "_" + std::to_string(entry.first) + "D";
                if (!used.insert(name).second) { result.addWarning("ASTER_DUPLICATE_GROUP", "Group name '" + name + "' appears twice; the second one was skipped"); continue; }
                mesh.addPhysicalGroup(name, entry.first, tag++, entry.second);
                ++result.elementSetsRead;
            }
        }
    }
    destination = std::move(mesh);
    return result;
}

} // namespace OpenModel
