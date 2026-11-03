#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/AbaqusInpWriter.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>

namespace OpenModel {
namespace {

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool parseNumber(const std::string& text, double& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

bool parseVector3(const std::string& text, std::array<double, 3>& out) {
    std::istringstream in(text);
    std::string cell;
    std::size_t n = 0;
    while (std::getline(in, cell, ',')) {
        if (n >= 3 || !parseNumber(cell, out[n])) return false;
        ++n;
    }
    return n == 3 && text.back() != ',';
}

struct Parser {
    AnalysisParseResult result;
    bool haveMaterial = false, haveStep = false;
    std::size_t line = 0;

    void error(std::string code, std::string message) {
        result.diagnostics.push_back({ImportDiagnosticLevel::Error, std::move(code), std::move(message), line});
    }
    void warning(std::string code, std::string message) {
        result.diagnostics.push_back({ImportDiagnosticLevel::Warning, std::move(code), std::move(message), line});
    }

    bool selector(const std::string& token, NodeSelector& out) {
        if (lowered(token) == "all") { out.kind = SelectorKind::All; return true; }
        if (startsWith(token, "group:") && token.size() > 6) {
            out.kind = SelectorKind::Group;
            out.name = token.substr(6);
            return true;
        }
        if (startsWith(token, "plane:") && token.size() > 8 && token[7] == '=') {
            const char axis = static_cast<char>(std::tolower(static_cast<unsigned char>(token[6])));
            if (axis != 'x' && axis != 'y' && axis != 'z') return false;
            out.kind = SelectorKind::Plane;
            out.axis = axis - 'x';
            return parseNumber(token.substr(8), out.position);
        }
        return false;
    }

    void parseLine(const std::string& raw) {
        std::string text = raw;
        const auto hash = text.find('#');
        if (hash != std::string::npos) text.erase(hash);
        std::istringstream in(text);
        std::vector<std::string> tokens;
        for (std::string t; in >> t;) tokens.push_back(t);
        if (tokens.empty()) return;
        const std::string command = lowered(tokens[0]);

        std::map<std::string, std::string> keys;
        std::vector<std::string> positional;
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            const std::string& t = tokens[i];
            if (startsWith(t, "plane:") || startsWith(t, "group:") || lowered(t) == "all") { positional.push_back(t); continue; }
            const auto eq = t.find('=');
            if (eq == std::string::npos || eq == 0) { positional.push_back(t); continue; }
            const std::string key = lowered(t.substr(0, eq));
            if (keys.count(key)) { error("SCRIPT_DUPLICATE_KEY", "'" + key + "' is given twice"); return; }
            keys[key] = t.substr(eq + 1);
        }
        auto take = [&](const char* key, std::string& value) {
            const auto it = keys.find(key);
            if (it == keys.end()) return false;
            value = it->second;
            keys.erase(it);
            return true;
        };
        auto number = [&](const char* key, double& value, bool required) {
            std::string text;
            if (!take(key, text)) {
                if (required) error("SCRIPT_MISSING_KEY", "'" + command + "' needs " + key + "=");
                return false;
            }
            if (!parseNumber(text, value)) { error("SCRIPT_BAD_NUMBER", "'" + text + "' is not a number (" + key + ")"); return false; }
            return true;
        };
        auto vector3 = [&](const char* key, std::array<double, 3>& value) {
            std::string text;
            if (!take(key, text)) { error("SCRIPT_MISSING_KEY", "'" + command + "' needs " + key + "=x,y,z"); return false; }
            if (!parseVector3(text, value)) { error("SCRIPT_BAD_VECTOR", "'" + text + "' is not three comma separated numbers (" + key + ")"); return false; }
            return true;
        };
        auto leftovers = [&]() {
            for (const auto& k : keys) error("SCRIPT_UNKNOWN_KEY", "Unknown option '" + k.first + "' for '" + command + "'");
        };
        const std::size_t errorsBefore = result.diagnostics.size();

        if (command == "material") {
            if (haveMaterial) { error("SCRIPT_DUPLICATE", "Only one material is supported"); return; }
            AnalysisMaterial m;
            number("e", m.youngModulus, true);
            number("nu", m.poisson, true);
            number("rho", m.density, false);
            std::string name;
            if (take("name", name)) m.name = name;
            if (!positional.empty()) error("SCRIPT_UNEXPECTED", "Unexpected '" + positional[0] + "' after material");
            leftovers();
            if (result.diagnostics.size() == errorsBefore) { result.model.material = m; haveMaterial = true; }
        } else if (command == "fix") {
            FixedSupport f;
            if (positional.size() != 1 || !selector(positional[0], f.where)) { error("SCRIPT_SELECTOR", "fix needs one selector: all, group:NAME or plane:x=VALUE"); return; }
            std::string dofs, tol;
            if (take("dof", dofs) && lowered(dofs) != "all") {
                f.dof = {{false, false, false}};
                std::istringstream d(dofs);
                std::string cell;
                while (std::getline(d, cell, ',')) {
                    if (cell == "1" || cell == "2" || cell == "3") f.dof[static_cast<std::size_t>(cell[0] - '1')] = true;
                    else { error("SCRIPT_BAD_DOF", "dof must list 1, 2, 3 (x, y, z) separated by commas, or 'all'"); return; }
                }
            }
            std::string prescribed;
            if (take("value", prescribed) && !parseVector3(prescribed, f.displacement))
                error("SCRIPT_BAD_VECTOR", "'" + prescribed + "' is not three comma separated prescribed translations (value)");
            if (take("tol", tol) && !parseNumber(tol, f.where.tolerance)) error("SCRIPT_BAD_NUMBER", "'" + tol + "' is not a number (tol)");
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.supports.push_back(f);
        } else if (command == "force") {
            NodalForce f;
            if (positional.size() != 1 || !selector(positional[0], f.where)) { error("SCRIPT_SELECTOR", "force needs one selector: all, group:NAME or plane:x=VALUE"); return; }
            vector3("f", f.total);
            std::string tol;
            if (take("tol", tol) && !parseNumber(tol, f.where.tolerance)) error("SCRIPT_BAD_NUMBER", "'" + tol + "' is not a number (tol)");
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.forces.push_back(f);
        } else if (command == "pressure") {
            SurfacePressure p;
            NodeSelector sel;
            if (positional.size() != 1 || !selector(positional[0], sel) || sel.kind != SelectorKind::Group) {
                error("SCRIPT_SELECTOR", "pressure needs a surface group: pressure group:NAME p=VALUE");
                return;
            }
            p.group = sel.name;
            number("p", p.pressure, true);
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.pressures.push_back(p);
        } else if (command == "gravity") {
            std::array<double, 3> g{};
            if (!positional.empty()) error("SCRIPT_UNEXPECTED", "Unexpected '" + positional[0] + "' after gravity");
            vector3("g", g);
            leftovers();
            if (result.diagnostics.size() == errorsBefore) {
                if (result.model.hasGravity) { error("SCRIPT_DUPLICATE", "Only one gravity line is supported"); return; }
                result.model.hasGravity = true;
                result.model.gravity = g;
            }
        } else if (command == "step") {
            if (haveStep) { error("SCRIPT_DUPLICATE", "Only one step is supported"); return; }
            if (positional.size() != 1) { error("SCRIPT_STEP", "step needs 'static' or 'frequency'"); return; }
            const std::string kind = lowered(positional[0]);
            if (kind == "static") result.model.step = StepType::Static;
            else if (kind == "frequency") {
                result.model.step = StepType::Frequency;
                double modes = 6;
                if (number("modes", modes, false)) {
                    if (modes < 1 || modes > 1000 || modes != std::floor(modes)) { error("SCRIPT_BAD_MODES", "modes must be an integer between 1 and 1000"); return; }
                    result.model.modes = static_cast<std::size_t>(modes);
                }
            } else { error("SCRIPT_STEP", "Unknown step '" + positional[0] + "' (static or frequency)"); return; }
            leftovers();
            haveStep = true;
        } else if (command == "output") {
            std::string element;
            if (take("element", element)) {
                const std::string v = lowered(element);
                if (v == "on") result.model.elementPrint = 1;
                else if (v == "off") result.model.elementPrint = 0;
                else if (v == "auto") result.model.elementPrint = -1;
                else error("SCRIPT_BAD_OUTPUT", "output element= takes on, off or auto");
            }
            leftovers();
        } else {
            error("SCRIPT_UNKNOWN_COMMAND", "Unknown command '" + tokens[0] + "' (material, fix, force, pressure, gravity, step, output)");
        }
    }
};

} // namespace

AnalysisParseResult parseAnalysisScript(const std::string& text) {
    Parser p;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        ++p.line;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        p.parseLine(raw);
    }
    p.line = 0;
    if (!p.haveMaterial) p.error("SCRIPT_NO_MATERIAL", "The script has no 'material' line");
    if (!p.haveStep) p.warning("SCRIPT_DEFAULT_STEP", "No 'step' line: a static analysis is assumed");
    p.result.success = std::none_of(p.result.diagnostics.begin(), p.result.diagnostics.end(),
                                    [](const ImportDiagnostic& d) { return d.level == ImportDiagnosticLevel::Error; });
    return std::move(p.result);
}

std::string exampleAnalysisScript() {
    return "# Cantilever: clamped at x = 0, load at x = 10 (units: mm, N, tonne, s)\n"
           "material E=210000 nu=0.3 rho=7.85e-9\n"
           "fix plane:x=0\n"
           "force plane:x=10 F=0,-100,0\n"
           "step static\n"
           "# step frequency modes=6\n";
}

std::vector<NodeIndex> selectNodes(const MeshData& mesh, const NodeSelector& selector, std::string& error) {
    std::vector<NodeIndex> out;
    error.clear();
    const std::size_t n = mesh.getNumNodes();
    if (selector.kind == SelectorKind::All) {
        out.resize(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<NodeIndex>(i);
        return out;
    }
    if (selector.kind == SelectorKind::Plane) {
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (std::size_t i = 0; i < n; ++i) {
            const auto c = mesh.nodeCoordinates(static_cast<NodeIndex>(i));
            for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
        }
        const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        const double tol = selector.tolerance >= 0.0 ? selector.tolerance : 1e-6 * std::max(diag, 1e-300);
        for (std::size_t i = 0; i < n; ++i)
            if (std::fabs(mesh.nodeCoordinates(static_cast<NodeIndex>(i))[static_cast<std::size_t>(selector.axis)] - selector.position) <= tol)
                out.push_back(static_cast<NodeIndex>(i));
        if (out.empty()) {
            std::ostringstream m;
            m << "No node lies on the plane " << "xyz"[selector.axis] << " = " << selector.position << " (tolerance " << tol << "; the model spans "
              << lo[selector.axis] << " .. " << hi[selector.axis] << " in that direction)";
            error = m.str();
        }
        return out;
    }
    // Group: prefer an explicit node set, then a generic mesh group, then
    // legacy physical groups. Generic node memberships are authoritative: they
    // preserve many-to-many CAD membership without reconstructing it from elements.
    const auto& sets = mesh.getNodeSets();
    auto set = sets.find(selector.name);
    if (set == sets.end())
        for (auto it = sets.begin(); it != sets.end(); ++it)
            if (lowered(it->first) == lowered(selector.name)) { set = it; break; }
    if (set != sets.end()) {
        out = set->second;
    } else {
        const MeshGroup* generic = mesh.getMeshGroupRegistry().find(selector.name);
        if (!generic)
            for (const auto& g : mesh.getMeshGroupRegistry().groups())
                if (lowered(g.name) == lowered(selector.name)) { generic = &g; break; }
        if (generic) {
            out = generic->nodeIndices;
            if (out.empty() && !generic->elementIds.empty()) {
                std::unordered_map<ElementId, char> ids;
                for (const ElementId id : generic->elementIds) ids.emplace(id, 1);
                for (const auto& block : mesh.getElementBlocks())
                    for (std::size_t e = 0; e < block.size(); ++e)
                        if (ids.count(block.ids[e]))
                            for (std::size_t k = 0; k < block.nodesPerElement; ++k)
                                out.push_back(block.connectivity[e * block.nodesPerElement + k]);
            }
        } else {
            const MeshPhysicalGroup* group = nullptr;
            for (const auto& g : mesh.getPhysicalGroups()) if (g.name == selector.name) { group = &g; break; }
            if (!group)
                for (const auto& g : mesh.getPhysicalGroups()) if (lowered(g.name) == lowered(selector.name)) { group = &g; break; }
            if (!group) {
                std::string known;
                for (const auto& g : mesh.getPhysicalGroups()) known += (known.empty() ? "" : ", ") + g.name;
                for (const auto& g : mesh.getMeshGroupRegistry().groups()) known += (known.empty() ? "" : ", ") + g.name;
                for (const auto& s : sets) known += (known.empty() ? "" : ", ") + s.first;
                error = "Unknown group or node set '" + selector.name + "'" + (known.empty() ? " (the mesh has none)" : " (available: " + known + ")");
                return out;
            }
            std::unordered_map<ElementId, char> ids;
            for (const ElementId id : group->elementIds) ids.emplace(id, 1);
            for (const auto& block : mesh.getElementBlocks())
                for (std::size_t e = 0; e < block.size(); ++e)
                    if (ids.count(block.ids[e]))
                        for (std::size_t k = 0; k < block.nodesPerElement; ++k)
                            out.push_back(block.connectivity[e * block.nodesPerElement + k]);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.empty() && error.empty()) error = "The group or node set '" + selector.name + "' contains no node";
    return out;
}

namespace {

/** Corner positions of the faces of an element type in CalculiX numbering (0-based); empty = pressure unsupported. */
std::vector<std::vector<int>> faceTable(ElementType type) {
    switch (type) {
    case ElementType::Tetrahedron4:
    case ElementType::Tetrahedron10:
        return {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
    case ElementType::Hexahedron8:
    case ElementType::Hexahedron20:
        return {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
    default:
        return {};
    }
}

int surfaceCorners(ElementType type) {
    switch (type) {
    case ElementType::Triangle3: case ElementType::Triangle6: return 3;
    case ElementType::Quadrilateral4: case ElementType::Quadrilateral8: case ElementType::Quadrilateral9: return 4;
    default: return 0;
    }
}

std::string idList(const MeshData& mesh, const std::vector<NodeIndex>& nodes) {
    std::ostringstream out;
    std::size_t count = 0;
    for (const NodeIndex n : nodes) {
        if (count) out << ", ";
        out << mesh.nodeId(n);
        if (++count == 16) { out << "\n"; count = 0; }
    }
    if (count) out << "\n";
    return out.str();
}

void note(SolverInputResult& r, ImportDiagnosticLevel level, std::string code, std::string message) {
    r.diagnostics.push_back({level, std::move(code), std::move(message), 0});
}

SolverInputResult failed(SolverInputResult r, std::string message) {
    r.success = false;
    r.error = std::move(message);
    return r;
}

} // namespace

SolverInputResult writeSolverInput(const MeshData& mesh, const AnalysisModel& model, const std::filesystem::path& path) {
    SolverInputResult r;
    const auto& m = model.material;
    if (!(m.youngModulus > 0.0) || !std::isfinite(m.youngModulus)) return failed(r, "Young's modulus must be positive");
    if (!(m.poisson > -1.0 && m.poisson < 0.5)) return failed(r, "Poisson's ratio must lie between -1 and 0.5 (exclusive)");
    const bool needDensity = model.step == StepType::Frequency || model.hasGravity;
    if (needDensity && !(m.density > 0.0)) return failed(r, "A frequency analysis and gravity need the density (rho=)");
    if (model.hasGravity && model.step == StepType::Frequency) note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_GRAVITY_IGNORED", "Loads (gravity included) have no effect in a frequency step");
    if (model.step == StepType::Frequency && (!model.forces.empty() || !model.pressures.empty()))
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_LOADS_IGNORED", "Forces and pressures have no effect in a frequency step");
    if (model.step == StepType::Static && model.supports.empty())
        return failed(r, "A static analysis needs at least one support (fix ...): without it the body can move freely and the solver cannot converge");
    if (model.step == StepType::Static && model.forces.empty() && model.pressures.empty() && !model.hasGravity)
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_NO_LOAD", "The static analysis has no load: all results will be zero");
    if (model.step == StepType::Frequency && model.supports.empty())
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_FREE_BODY", "Frequency analysis without supports: the first modes are rigid body motions with zero frequency");

    // Volume part of the mesh only.
    MeshData solid;
    std::vector<std::string> volumeSets;
    for (std::size_t i = 0; i < mesh.getNumNodes(); ++i) {
        const auto c = mesh.nodeCoordinates(static_cast<NodeIndex>(i));
        solid.addNode(mesh.nodeId(static_cast<NodeIndex>(i)), c[0], c[1], c[2]);
    }
    std::vector<ElementId> volumeIds;
    std::unordered_map<std::string, std::pair<ElementType, std::size_t>> blockSeen;
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 3) continue;
        if (AbaqusInpWriter::abaqusElementType(block.type).empty())
            return failed(r, "Element type of block '" + block.name + "' cannot be written for CalculiX");
        for (std::size_t e = 0; e < block.size(); ++e) {
            std::vector<NodeIndex> conn(block.connectivity.begin() + static_cast<std::ptrdiff_t>(e * block.nodesPerElement),
                                        block.connectivity.begin() + static_cast<std::ptrdiff_t>((e + 1) * block.nodesPerElement));
            solid.addElement(block.name, block.type, block.ids[e], conn);
            volumeIds.push_back(block.ids[e]);
        }
        volumeSets.push_back(AbaqusInpWriter::sanitizeName(block.name));
    }
    if (volumeIds.empty()) return failed(r, "The mesh has no volume elements: generate a volume mesh first (surface meshes cannot be analysed here)");
    {
        std::unordered_map<ElementId, char> inVolume;
        for (const ElementId id : volumeIds) inVolume.emplace(id, 1);
        for (const auto& g : mesh.getPhysicalGroups()) {
            if (g.dimension != 3) continue;
            std::vector<ElementId> ids;
            for (const ElementId id : g.elementIds) if (inVolume.count(id)) ids.push_back(id);
            solid.addPhysicalGroup(g.name, g.dimension, g.tag, ids, g.materialName);
        }
    }
    r.nodes = solid.getNumNodes();
    r.elements = volumeIds.size();

    // Resolve conditions.
    std::string error;
    std::vector<std::vector<NodeIndex>> supportNodes, forceNodes;
    std::vector<char> constrained(mesh.getNumNodes(), 0);
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        supportNodes.push_back(selectNodes(mesh, model.supports[i].where, error));
        if (!error.empty()) return failed(r, "fix #" + std::to_string(i + 1) + ": " + error);
        for (const NodeIndex n : supportNodes.back()) constrained[n] = 1;
        if (!model.supports[i].dof[0] && !model.supports[i].dof[1] && !model.supports[i].dof[2])
            return failed(r, "fix #" + std::to_string(i + 1) + " constrains no direction");
    }
    for (const char c : constrained) r.fixedNodes += c ? 1 : 0;
    std::size_t quadratic = 0;
    for (const auto& b : solid.getElementBlocks()) quadratic += (b.type == ElementType::Tetrahedron10 || b.type == ElementType::Hexahedron20) ? 1 : 0;
    for (std::size_t i = 0; i < model.forces.size(); ++i) {
        forceNodes.push_back(selectNodes(mesh, model.forces[i].where, error));
        if (!error.empty()) return failed(r, "force #" + std::to_string(i + 1) + ": " + error);
        r.loadedNodes += forceNodes.back().size();
    }
    if (quadratic && !model.forces.empty())
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_FORCE_QUADRATIC",
             "A force shared equally by all nodes of a quadratic mesh is not the consistent load (corner nodes should carry less than mid-side nodes); use 'pressure' on a surface group for accurate results");

    // Pressure: faces of volume elements that coincide with the surface elements of the group.
    struct FaceLoad { ElementId element; int face; double pressure; };
    std::vector<FaceLoad> faceLoads;
    if (!model.pressures.empty()) {
        std::map<std::array<NodeIndex, 4>, std::vector<std::pair<ElementId, int>>> faces;
        for (const auto& block : solid.getElementBlocks()) {
            const auto table = faceTable(block.type);
            if (table.empty()) continue;
            for (std::size_t e = 0; e < block.size(); ++e)
                for (std::size_t f = 0; f < table.size(); ++f) {
                    std::array<NodeIndex, 4> key{{~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0)}};
                    for (std::size_t k = 0; k < table[f].size(); ++k) key[k] = block.connectivity[e * block.nodesPerElement + static_cast<std::size_t>(table[f][k])];
                    std::sort(key.begin(), key.begin() + static_cast<std::ptrdiff_t>(table[f].size()));
                    faces[key].push_back({block.ids[e], static_cast<int>(f) + 1});
                }
        }
        std::unordered_map<ElementId, std::pair<const ElementBlock*, std::size_t>> location;
        for (const auto& block : mesh.getElementBlocks())
            for (std::size_t e = 0; e < block.size(); ++e) location.emplace(block.ids[e], std::make_pair(&block, e));
        for (const auto& p : model.pressures) {
            const std::vector<ElementId>* groupElements = nullptr;
            int groupDimension = -1;
            for (const auto& g : mesh.getPhysicalGroups())
                if (g.name == p.group) { groupElements = &g.elementIds; groupDimension = g.dimension; break; }
            if (!groupElements)
                for (const auto& g : mesh.getPhysicalGroups())
                    if (lowered(g.name) == lowered(p.group)) { groupElements = &g.elementIds; groupDimension = g.dimension; break; }
            if (!groupElements) {
                const MeshGroup* generic = mesh.getMeshGroupRegistry().find(p.group);
                if (!generic)
                    for (const auto& g : mesh.getMeshGroupRegistry().groups())
                        if (lowered(g.name) == lowered(p.group)) { generic = &g; break; }
                if (generic) { groupElements = &generic->elementIds; groupDimension = generic->dimension; }
            }
            if (!groupElements) return failed(r, "pressure: unknown surface group '" + p.group + "'");
            if (groupDimension != 2) return failed(r, "pressure: group '" + p.group + "' is not a surface group (dimension " + std::to_string(groupDimension) + ")");
            if (groupElements->empty()) return failed(r, "pressure: surface group '" + p.group + "' has no surface elements");
            std::size_t unmatched = 0, shared = 0;
            for (const ElementId id : *groupElements) {
                const auto at = location.find(id);
                const int corners = at == location.end() ? 0 : surfaceCorners(at->second.first->type);
                if (corners == 0) { ++unmatched; continue; }
                const auto& block = *at->second.first;
                std::array<NodeIndex, 4> key{{~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0)}};
                for (int k = 0; k < corners; ++k) key[static_cast<std::size_t>(k)] = block.connectivity[at->second.second * block.nodesPerElement + static_cast<std::size_t>(k)];
                std::sort(key.begin(), key.begin() + corners);
                const auto hit = faces.find(key);
                if (hit == faces.end()) { ++unmatched; continue; }
                if (hit->second.size() != 1) { ++shared; continue; }
                faceLoads.push_back({hit->second[0].first, hit->second[0].second, p.pressure});
            }
            if (unmatched) return failed(r, "pressure: " + std::to_string(unmatched) + " surface element(s) of '" + p.group +
                                              "' are not faces of a tetrahedron/hexahedron (other element types are not supported for pressure)");
            if (shared) return failed(r, "pressure: " + std::to_string(shared) + " surface element(s) of '" + p.group +
                                           "' lie between two volume elements; a pressure needs a boundary surface");
        }
        r.pressureFaces = faceLoads.size();
    }

    // Write: the mesh through the existing writer, then the model.
    {
        AbaqusWriteOptions options;
        options.writeHeading = false;
        const auto w = AbaqusInpWriter::writeFile(path.string(), solid, options);
        if (!w.success) return failed(r, w.errors.empty() ? "Cannot write the mesh" : w.errors.front());
    }
    std::ostringstream out;
    out << std::setprecision(10);
    out << "** OpenModel analysis: " << (model.step == StepType::Static ? "linear static" : "frequency") << "\n";
    out << "*NSET, NSET=MG_NALL\n";
    {
        std::vector<NodeIndex> all(mesh.getNumNodes());
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<NodeIndex>(i);
        out << idList(mesh, all);
    }
    out << "*ELSET, ELSET=MG_EALL\n";
    for (std::size_t i = 0; i < volumeSets.size(); ++i) out << volumeSets[i] << (i + 1 < volumeSets.size() ? ",\n" : "\n");
    out << "*MATERIAL, NAME=" << AbaqusInpWriter::sanitizeName(m.name.empty() ? "MATERIAL" : m.name) << "\n*ELASTIC\n"
        << m.youngModulus << ", " << m.poisson << "\n";
    if (m.density > 0.0) out << "*DENSITY\n" << m.density << "\n";
    const std::string materialName = AbaqusInpWriter::sanitizeName(m.name.empty() ? "MATERIAL" : m.name);
    for (const auto& setName : volumeSets) out << "*SOLID SECTION, ELSET=" << setName << ", MATERIAL=" << materialName << "\n";
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        out << "*NSET, NSET=MG_FIX" << i + 1 << "\n" << idList(mesh, supportNodes[i]);
    }
    for (std::size_t i = 0; i < model.forces.size(); ++i) out << "*NSET, NSET=MG_LOAD" << i + 1 << "\n" << idList(mesh, forceNodes[i]);
    if (!model.supports.empty()) {
        out << "*BOUNDARY\n";
        for (std::size_t i = 0; i < model.supports.size(); ++i)
            for (int d = 0; d < 3; ++d)
                if (model.supports[i].dof[static_cast<std::size_t>(d)])
                    out << "MG_FIX" << i + 1 << ", " << d + 1 << ", " << d + 1 << ", "
                        << model.supports[i].displacement[static_cast<std::size_t>(d)] << "\n";
    }
    out << "*STEP\n";
    if (model.step == StepType::Static) {
        out << "*STATIC\n";
        if (!model.forces.empty()) {
            out << "*CLOAD\n";
            for (std::size_t i = 0; i < model.forces.size(); ++i)
                for (int d = 0; d < 3; ++d) {
                    const double share = model.forces[i].total[static_cast<std::size_t>(d)] / static_cast<double>(forceNodes[i].size());
                    if (share != 0.0) out << "MG_LOAD" << i + 1 << ", " << d + 1 << ", " << share << "\n";
                }
        }
        if (!faceLoads.empty() || model.hasGravity) {
            out << "*DLOAD\n";
            for (const auto& f : faceLoads) out << f.element << ", P" << f.face << ", " << f.pressure << "\n";
            if (model.hasGravity) {
                const auto& g = model.gravity;
                const double magnitude = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
                if (magnitude > 0.0) out << "MG_EALL, GRAV, " << magnitude << ", " << g[0] / magnitude << ", " << g[1] / magnitude << ", " << g[2] / magnitude << "\n";
            }
        }
    } else {
        out << "*FREQUENCY\n" << model.modes << "\n";
    }
    out << "*NODE FILE\nU" << (model.step == StepType::Static ? ", S, E" : "") << "\n";
    const bool print = model.step == StepType::Static && (model.elementPrint == 1 || (model.elementPrint < 0 && volumeIds.size() <= 200000));
    if (model.step == StepType::Static && !print)
        note(r, ImportDiagnosticLevel::Info, "ANALYSIS_NO_ELEMENT_PRINT",
             "Element stresses are not written to the .dat (" + std::to_string(volumeIds.size()) + " elements; use 'output element=on' to force); the nodal ones are in the .frd");
    if (print) out << "*EL PRINT, ELSET=MG_EALL\nS, E\n";
    out << "*END STEP\n";

    {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (!file) return failed(r, "Cannot append to " + path.string());
        const std::string text = out.str();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) return failed(r, "Failed while writing " + path.string());
    }
    std::ostringstream summary;
    summary << (model.step == StepType::Static ? "Static analysis" : "Frequency analysis (" + std::to_string(model.modes) + " modes)") << " of "
            << r.elements << " volume elements, " << r.nodes << " nodes; E = " << m.youngModulus << ", nu = " << m.poisson;
    if (r.fixedNodes) summary << "; " << r.fixedNodes << " constrained nodes";
    if (r.loadedNodes) summary << "; force on " << r.loadedNodes << " nodes";
    if (r.pressureFaces) summary << "; pressure on " << r.pressureFaces << " faces";
    if (model.hasGravity) summary << "; gravity";
    r.summary = summary.str();
    r.success = true;
    return r;
}

} // namespace OpenModel
