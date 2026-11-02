/**
 * @file mesh_selection_test.cpp
 * @brief Tests of group creation from viewer selections (node sets, element groups, face groups) and their use in an analysis.
 */
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/MeshSelection.hpp>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace OpenModel;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool contains(const std::string& t, const std::string& w) { return t.find(w) != std::string::npos; }

/** Two stacked unit hexahedra along z; node ids 101.. (z-major), element ids 7 and 8. */
MeshData makeColumn() {
    MeshData m;
    for (int z = 0; z <= 2; ++z)
        for (int y = 0; y <= 1; ++y)
            for (int x = 0; x <= 1; ++x) m.addNode(101 + z * 4 + y * 2 + x, x, y, z);
    auto n = [](int x, int y, int z) { return static_cast<NodeIndex>(z * 4 + y * 2 + x); };
    for (int z = 0; z < 2; ++z)
        m.addElement("solid", ElementType::Hexahedron8, 7 + z,
                     {n(0, 0, z), n(1, 0, z), n(1, 1, z), n(0, 1, z), n(0, 0, z + 1), n(1, 0, z + 1), n(1, 1, z + 1), n(0, 1, z + 1)});
    return m;
}

ViewerSelection nodes(std::vector<NodeId> ids) { ViewerSelection s; s.mode = "node"; s.nodes = std::move(ids); return s; }
ViewerSelection elements(std::vector<ElementId> ids) { ViewerSelection s; s.mode = "element"; s.elements = std::move(ids); return s; }
ViewerSelection face(ElementId e, NodeId a, NodeId b, NodeId c) { ViewerSelection s; s.mode = "face"; s.faces.push_back({e, {{a, b, c}}}); return s; }

std::string slurp(const fs::path& p) { std::ifstream f(p); std::ostringstream s; s << f.rdbuf(); return s.str(); }

void testNames() {
    CHECK(isValidGroupName("Fix_1.a-b"));
    CHECK(!isValidGroupName("") && !isValidGroupName("a b") && !isValidGroupName("a=b") && !isValidGroupName("é") && !isValidGroupName(std::string(65, 'a')));
    MeshData m = makeColumn();
    CHECK(createGroup(m, "A", GroupKind::NodeSet, nodes({101})).success);
    CHECK(groupNameInUse(m, "a"));                                   // case-insensitive
    CHECK(!createGroup(m, "a", GroupKind::NodeSet, nodes({102})).success);
    CHECK(uniqueGroupName(m, "A") == "A1" && uniqueGroupName(m, "B") == "B1");
    CHECK(groupNameInUse(m, "solid"));                               // element block names are taken too
}

void testNodesAndElements() {
    MeshData m = makeColumn();
    const auto before = m.getNumElements();
    auto r = createGroup(m, "Base", GroupKind::NodeSet, nodes({101, 102, 103, 104, 101}));
    CHECK(r.success && r.count == 4);
    CHECK(m.getNodeSets().at("Base").size() == 4);
    auto bad = createGroup(m, "Bad", GroupKind::NodeSet, nodes({101, 999}));
    CHECK(!bad.success && contains(bad.error, "999") && !m.getNodeSets().count("Bad"));   // nothing is created on error
    CHECK(!createGroup(m, "E", GroupKind::NodeSet, nodes({})).success);
    CHECK(!createGroup(m, "E", GroupKind::NodeSet, ViewerSelection{}).success);
    CHECK(!createGroup(m, "E", GroupKind::ElementGroup, nodes({101})).success);              // elements cannot come from nodes
    CHECK(!createGroup(m, "E", GroupKind::FaceGroup, elements({7})).success);               // faces need faces

    auto e = createGroup(m, "Lower", GroupKind::ElementGroup, elements({7}));
    CHECK(e.success && e.count == 1);
    const auto& groups = m.getPhysicalGroups();
    CHECK(groups.size() == 1 && groups[0].name == "Lower" && groups[0].dimension == 3 && groups[0].elementIds == std::vector<ElementId>{7});
    CHECK(!createGroup(m, "Ghost", GroupKind::ElementGroup, elements({7, 55})).success);
    auto ns = createGroup(m, "LowerNodes", GroupKind::NodeSet, elements({7}));
    CHECK(ns.success && ns.count == 8);                                // nodes of an element
    auto both = createGroup(m, "AllNodes", GroupKind::NodeSet, elements({7, 8}));
    CHECK(both.success && both.count == 12);                           // shared nodes counted once
    CHECK(m.getNumElements() == before);                               // node and element groups add no elements
}

/** Outward normal of a created surface element, from its coordinates. */
std::array<double, 3> normalOf(const MeshData& m, ElementId id) {
    for (const auto& b : m.getElementBlocks())
        for (std::size_t e = 0; e < b.size(); ++e)
            if (b.ids[e] == id) {
                const auto p0 = m.nodeCoordinates(b.connectivity[e * b.nodesPerElement]), p1 = m.nodeCoordinates(b.connectivity[e * b.nodesPerElement + 1]),
                           p2 = m.nodeCoordinates(b.connectivity[e * b.nodesPerElement + 2]);
                const std::array<double, 3> u{{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}}, v{{p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]}};
                return {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
            }
    return {0, 0, 0};
}

void testFaces() {
    MeshData m = makeColumn();
    // Top of element 8 (z = 2): nodes 109..112; any three corners will do. Bottom of element 7 (z = 0): 101..104.
    auto top = createGroup(m, "Top", GroupKind::FaceGroup, face(8, 109, 110, 112));
    CHECK(top.success && top.count == 1);
    auto bottom = createGroup(m, "Bottom", GroupKind::FaceGroup, face(7, 102, 103, 104));
    CHECK(bottom.success);
    const auto& g = m.getPhysicalGroups();
    CHECK(g.size() == 2 && g[0].dimension == 2 && g[1].dimension == 2);
    CHECK(m.getNumElements() == 4);
    const auto nt = normalOf(m, g[0].elementIds[0]), nb = normalOf(m, g[1].elementIds[0]);
    CHECK(nt[2] > 0.5 && std::fabs(nt[0]) < 1e-12 && std::fabs(nt[1]) < 1e-12);      // outward = +z on top, whatever the input order
    CHECK(nb[2] < -0.5 && std::fabs(nb[0]) < 1e-12 && std::fabs(nb[1]) < 1e-12);
    CHECK(g[0].elementIds[0] != g[1].elementIds[0] && g[0].elementIds[0] > 8);        // new ids never collide
    // A quad becomes a Quadrilateral4 block.
    bool quad = false;
    for (const auto& b : m.getElementBlocks()) quad = quad || (b.name == "Top_quad" && b.type == ElementType::Quadrilateral4 && b.size() == 1);
    CHECK(quad);
    // Same face twice and the four triples of one quad count once.
    auto twice = createGroup(m, "Sides", GroupKind::FaceGroup, [] { ViewerSelection s = face(7, 101, 102, 106); s.faces.push_back({7, {{101, 102, 105}}}); s.faces.push_back({7, {{102, 105, 106}}}); return s; }());
    CHECK(twice.success && twice.count == 1);
    // Wrong nodes (not one face: 101 and 108 are opposite corners of element 7 together with 102).
    auto wrong = createGroup(m, "Wrong", GroupKind::FaceGroup, face(7, 101, 102, 108));
    CHECK(!wrong.success && contains(wrong.error, "not corners of one face") && m.getNumElements() == 5 && !groupNameInUse(m, "Wrong"));
    CHECK(!createGroup(m, "Wrong2", GroupKind::FaceGroup, face(8, 101, 102, 103)).success);   // face of the other element
    // Nodes of a face, and the owner element of a face.
    auto fn = createGroup(m, "TopNodes", GroupKind::NodeSet, face(8, 109, 110, 112));
    CHECK(fn.success && fn.count == 4 && m.getNodeSets().at("TopNodes").size() == 4);
    auto fe = createGroup(m, "TopElement", GroupKind::ElementGroup, face(8, 109, 110, 112));
    CHECK(fe.success && fe.count == 1);

    // listGroups and the way back to a viewer selection.
    const auto list = listGroups(m);
    std::set<std::string> names;
    for (const auto& i : list) names.insert(i.name + ":" + groupKindName(i.kind) + ":" + std::to_string(i.count));
    CHECK(names.count("Top:face:1") && names.count("TopNodes:node:4") && names.count("TopElement:element:1"));
    ViewerSelection back;
    std::size_t skipped = 0;
    std::string error;
    CHECK(groupAsSelection(m, "top", back, skipped, error) && back.mode == "face" && back.faces.size() == 1 && skipped == 0);
    CHECK(back.faces[0].element == 8);
    std::set<NodeId> ids(back.faces[0].nodes.begin(), back.faces[0].nodes.end());
    CHECK(ids.size() == 3 && std::all_of(ids.begin(), ids.end(), [](NodeId n) { return n >= 109 && n <= 112; }));
    CHECK(groupAsSelection(m, "TopNodes", back, skipped, error) && back.mode == "node" && back.nodes.size() == 4);
    CHECK(groupAsSelection(m, "TopElement", back, skipped, error) && back.mode == "element" && back.elements == std::vector<ElementId>{8});
    CHECK(!groupAsSelection(m, "Nope", back, skipped, error) && contains(error, "Unknown group"));
}

void testQuadratic() {
    // One quadratic tetrahedron in two common mid-side orderings: the face node set must hold 3 corners + 3 mid-side nodes.
    const double c[4][3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0, 0, 2}};
    const int edges[2][6][2] = {{{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}},       // Abaqus-like
                                {{0, 1}, {1, 2}, {0, 2}, {0, 3}, {2, 3}, {1, 3}}};      // Gmsh-like
    for (int order = 0; order < 2; ++order) {
        MeshData m;
        for (int i = 0; i < 4; ++i) m.addNode(1 + i, c[i][0], c[i][1], c[i][2]);
        for (int k = 0; k < 6; ++k) {
            const auto a = edges[order][k][0], b = edges[order][k][1];
            m.addNode(11 + k, (c[a][0] + c[b][0]) / 2, (c[a][1] + c[b][1]) / 2, (c[a][2] + c[b][2]) / 2);
        }
        m.addElement("quad", ElementType::Tetrahedron10, 1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
        // Face z = 0 holds corners 1,2,3 and the mid-side nodes of the edges among them.
        auto r = createGroup(m, "Foot", GroupKind::NodeSet, face(1, 1, 2, 3));
        CHECK(r.success && r.count == 6);
        std::set<double> z;
        for (const auto n : m.getNodeSets().at("Foot")) z.insert(m.nodeCoordinates(n)[2]);
        CHECK(z.size() == 1 && *z.begin() == 0.0);                       // all six lie in the face plane
        auto f = createGroup(m, "FootFace", GroupKind::FaceGroup, face(1, 1, 2, 3));
        CHECK(f.success && f.count == 1);
    }
}

void testWithAnalysis() {
    // The groups made from picks drive an analysis: supports on a node set, pressure on a face group.
    MeshData m = makeColumn();
    CHECK(createGroup(m, "Base", GroupKind::NodeSet, face(7, 101, 102, 103)).success);   // bottom face nodes 101..104
    CHECK(createGroup(m, "Top", GroupKind::FaceGroup, face(8, 109, 110, 111)).success);
    CHECK(createGroup(m, "Side", GroupKind::FaceGroup, face(7, 102, 104, 106)).success);   // face x = 1 of element 7: nodes 102,104,106,108
    const auto parsed = parseAnalysisScript("material E=1000 nu=0.3\nfix group:Base\npressure group:Top p=3\npressure group:Side p=2\n");
    CHECK(parsed.success);
    const fs::path dir = fs::temp_directory_path() / ("mg_sel_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const auto r = writeSolverInput(m, parsed.model, dir / "job.inp");
    CHECK(r.success && r.pressureFaces == 2 && r.fixedNodes == 4);
    const auto text = slurp(dir / "job.inp");
    // Independent face numbers: element 8 top = P2 (nodes 5-8), element 7 face x=1 = nodes 2,3,7,6 = P4.
    CHECK(contains(text, "8, P2, 3") && contains(text, "7, P4, 2"));
    CHECK(!contains(text, "TYPE=S3") && !contains(text, "TYPE=S4"));                      // face elements are not structure
    fs::remove_all(dir);
}

} // namespace

int main() {
    testNames();
    testNodesAndElements();
    testFaces();
    testQuadratic();
    testWithAnalysis();
    if (g_failures) { std::cerr << g_failures << " check(s) failed\n"; return 1; }
    std::cout << "mesh_selection_test: all checks passed\n";
    return 0;
}
