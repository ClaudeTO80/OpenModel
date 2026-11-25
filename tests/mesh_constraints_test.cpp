/**
 * @file mesh_constraints_test.cpp
 * @brief Tests of the constraints on the degrees of freedom of the nodes: MeshData API, MGOM 1.1, Abaqus/CalculiX
 *        `*BOUNDARY` import and export, the constraints that the supports of an analysis put on a mesh, and their MGWV chunk.
 */
#include <OpenModel/AbaqusInpWriter.hpp>
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/CodeAster.hpp>
#include <OpenModel/MeshSelection.hpp>
#include <OpenModel/ModelBinaryIO.hpp>
#include <OpenModel/MeshImporter.hpp>
#include <OpenModel/WebViewWriter.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

template <class Fn> bool throws(Fn&& fn) {
    try { fn(); } catch (const std::exception&) { return true; }
    return false;
}

/** A cube of n x n x n hexahedra on [0,n]^3, node ids 100 + index (so that ids and indices differ). */
MeshData makeCube(std::size_t n) {
    MeshData mesh;
    const std::size_t s = n + 1;
    for (std::size_t z = 0; z <= n; ++z)
        for (std::size_t y = 0; y <= n; ++y)
            for (std::size_t x = 0; x <= n; ++x)
                mesh.addNode(100 + (z * s + y) * s + x, double(x), double(y), double(z));
    auto idx = [s](std::size_t x, std::size_t y, std::size_t z) { return static_cast<NodeIndex>((z * s + y) * s + x); };
    ElementId id = 1;
    for (std::size_t z = 0; z < n; ++z)
        for (std::size_t y = 0; y < n; ++y)
            for (std::size_t x = 0; x < n; ++x)
                mesh.addElement("CUBE", ElementType::Hexahedron8, id++,
                                {idx(x, y, z), idx(x + 1, y, z), idx(x + 1, y + 1, z), idx(x, y + 1, z),
                                 idx(x, y, z + 1), idx(x + 1, y, z + 1), idx(x + 1, y + 1, z + 1), idx(x, y + 1, z + 1)});
    return mesh;
}

fs::path tempDir() {
    const auto dir = fs::temp_directory_path() / "openmodel_constraints_test";
    fs::create_directories(dir);
    return dir;
}

std::vector<unsigned char> slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void testApi() {
    auto mesh = makeCube(1);
    CHECK(mesh.constrainedNodeCount() == 0 && mesh.constrainedDofCount() == 0 && mesh.constraintOf(0) == nullptr);

    mesh.constrainNode(0, DofUx | DofUy);
    CHECK(mesh.constrainedNodeCount() == 1 && mesh.constrainedDofCount() == 2);
    mesh.constrainNode(0, DofUz, {0, 0, 0.5});                       // accumulates, keeps ux and uy
    const auto* c = mesh.constraintOf(0);
    CHECK(c && c->node == 0 && c->dofs == kDofTranslations && c->values[2] == 0.5 && c->values[0] == 0.0);
    mesh.constrainNode(0, DofUz, {0, 0, -2.0});                      // a constrained dof takes the new value
    CHECK(mesh.constraintOf(0)->values[2] == -2.0 && mesh.constrainedDofCount() == 3);
    CHECK(mesh.constraintOf(0)->dofs == kDofTranslations);

    mesh.releaseNode(0, DofUz);
    CHECK(mesh.constraintOf(0)->dofs == (DofUx | DofUy) && mesh.constraintOf(0)->values[2] == 0.0);
    mesh.releaseNode(0, DofUx | DofUy | DofRx);
    CHECK(mesh.constrainedNodeCount() == 0);                         // no dof left: the constraint is gone
    mesh.releaseNode(5);                                             // nothing to release: no effect, no throw

    mesh.constrainNodes({1, 2, 3}, kDofAll);
    CHECK(mesh.constrainedNodeCount() == 3 && mesh.constrainedDofCount() == 18);
    CHECK(mesh.getConstraints().begin()->first == 1);                // ordered by node index
    CHECK(mesh.validate().empty());

    // Invalid arguments throw and leave the constraints as they were.
    CHECK(throws([&] { mesh.constrainNode(99, DofUx); }));
    CHECK(throws([&] { mesh.constrainNode(0, 0); }));
    CHECK(throws([&] { mesh.constrainNode(0, 0x40); }));
    CHECK(throws([&] { mesh.constrainNode(0, DofUx, {std::nan(""), 0, 0, 0, 0, 0}); }));
    CHECK(throws([&] { mesh.constrainNodes({4, 99}, DofUx); }));
    CHECK(mesh.constraintOf(4) == nullptr && mesh.constrainedNodeCount() == 3);
    mesh.constrainNode(0, DofUx, {0, std::nan(""), 0, 0, 0, 0});     // a value on a dof that is not constrained is not read
    CHECK(mesh.constraintOf(0)->values[1] == 0.0);

    mesh.clearConstraints();
    CHECK(mesh.constrainedNodeCount() == 0);
    mesh.constrainNode(2, DofRz);
    mesh.clear();
    CHECK(mesh.constrainedNodeCount() == 0);                         // clear() drops them with the nodes
}

void testMgom() {
    const auto dir = tempDir();
    // Without constraints the file is written as MGOM 1.1 (minor version 1) and reads back without constraints.
    auto plain = makeCube(2);
    ModelBinaryIO::write(dir / "plain.omb", plain);
    const auto bytes = slurp(dir / "plain.omb");
    std::uint16_t minor = 0;
    std::memcpy(&minor, bytes.data() + 10, 2);
    CHECK(minor == 1);
    CHECK(ModelBinaryIO::read(dir / "plain.omb").constrainedNodeCount() == 0);

    auto mesh = makeCube(2);
    mesh.constrainNodes({0, 1, 2}, kDofTranslations);
    mesh.constrainNode(5, DofUx | DofRz, {0.25, 0, 0, 0, 0, -0.125});
    mesh.constrainNode(26, kDofAll);
    ModelBinaryIO::write(dir / "constrained.omb", mesh);
    const auto back = ModelBinaryIO::read(dir / "constrained.omb");
    CHECK(back.constrainedNodeCount() == 5 && back.constrainedDofCount() == mesh.constrainedDofCount());
    bool same = true;
    for (const auto& entry : mesh.getConstraints()) {
        const auto* c = back.constraintOf(entry.first);
        same = same && c && c->dofs == entry.second.dofs && c->values == entry.second.values;
    }
    CHECK(same);
    CHECK(back.constraintOf(5)->values[0] == 0.25 && back.constraintOf(5)->values[5] == -0.125);
    CHECK(back.getNumNodes() == mesh.getNumNodes() && back.getNumElements() == mesh.getNumElements());

    // A damaged constraint or body payload is refused: the last payload byte before the directory is covered by
    // the section CRC, so the flip is detected before any record is parsed.
    const auto withBytes = slurp(dir / "constrained.omb");
    std::uint64_t directoryOffset = 0;
    std::memcpy(&directoryOffset, withBytes.data() + 48, 8);
    CHECK(directoryOffset > 64 && directoryOffset < withBytes.size());
    {
        auto bad = withBytes;
        bad[static_cast<std::size_t>(directoryOffset) - 1] ^= 0x01;
        std::ofstream(dir / "bad_payload.omb", std::ios::binary).write(reinterpret_cast<const char*>(bad.data()), static_cast<std::streamsize>(bad.size()));
        CHECK(throws([&] { ModelBinaryIO::read(dir / "bad_payload.omb"); }));
    }
}

void testInpImport() {
    const auto dir = tempDir();
    const auto path = dir / "bc.inp";
    {
        std::ofstream f(path);
        f << "*HEADING\nx\n*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,0,0,1\n6,1,0,1\n7,1,1,1\n8,0,1,1\n"
             "*ELEMENT, TYPE=C3D8, ELSET=SOLID\n1,1,2,3,4,5,6,7,8\n"
             "*NSET, NSET=Left\n1,4,5,8\n"
             "*NSET, NSET=right\n2,3,6,7\n"
             "*BOUNDARY\n"
             "left, 1, 3\n"                       // nset, case-insensitive: ux uy uz
             "2, 2\n"                             // one node, uy only
             "RIGHT, 1, 1, 0.5\n"                 // prescribed ux = 0.5
             "6, ZSYMM\n"                         // uz, rx, ry
             "7, 11, 11, 20.0\n"                  // temperature: not a structural dof
             "*STEP\n*STATIC\n*BOUNDARY, TYPE=VELOCITY\n8, 1, 1, 3.0\n*END STEP\n";
    }
    AbaqusInpImporter importer;
    MeshData mesh;
    const auto result = importer.read(path, mesh);
    CHECK(result.status == ImportStatus::Success);
    CHECK(mesh.nodeId(0) == 1);
    const auto at = [&](NodeId id) { return mesh.constraintOf(mesh.nodeIndex(id)); };
    CHECK(at(1) && at(1)->dofs == kDofTranslations && at(1)->values[0] == 0.0);
    CHECK(at(2) && at(2)->dofs == (DofUx | DofUy) && at(2)->values[0] == 0.5 && at(2)->values[1] == 0.0);
    CHECK(at(3) && at(3)->dofs == DofUx && at(3)->values[0] == 0.5);
    CHECK(at(6) && at(6)->dofs == (DofUx | DofUz | DofRx | DofRy) && at(6)->values[0] == 0.5);
    CHECK(at(7) && at(7)->dofs == DofUx);                     // the temperature record added nothing
    CHECK(at(8) && at(8)->dofs == kDofTranslations);          // the velocity record was not imported
    CHECK(mesh.constrainedNodeCount() == 8 && result.constrainedNodesRead == 8);
    bool warned = false;
    for (const auto& d : result.diagnostics) warned = warned || d.code == "BOUNDARY_TYPE";
    CHECK(warned);

    // The option switches it off.
    MeshImportOptions off;
    off.importConstraints = false;
    MeshData none;
    CHECK(importer.read(path, none, off).status == ImportStatus::Success && none.constrainedNodeCount() == 0);

    // Errors: unknown node, unknown set, bad dof.
    const char* bad[] = {"99, 1, 3\n", "nowhere, 1, 3\n", "1, x\n", "1, 3, 1\n"};
    for (const char* line : bad) {
        const auto badPath = dir / "bad_bc.inp";
        std::ofstream(badPath) << "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n4,0,0,1\n*ELEMENT, TYPE=C3D4, ELSET=S\n1,1,2,3,4\n*BOUNDARY\n" << line;
        MeshData m;
        CHECK(importer.read(badPath, m).status != ImportStatus::Success);
    }
    // OP=NEW starts afresh.
    {
        const auto p = dir / "new_bc.inp";
        std::ofstream(p) << "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n4,0,0,1\n*ELEMENT, TYPE=C3D4, ELSET=S\n1,1,2,3,4\n*BOUNDARY\n1,1,3\n*BOUNDARY, OP=NEW\n2,2\n";
        MeshData m;
        CHECK(importer.read(p, m).status == ImportStatus::Success);
        CHECK(m.constrainedNodeCount() == 1 && m.constraintOf(m.nodeIndex(2)) != nullptr);
    }
}

void testInpRoundTrip() {
    const auto dir = tempDir();
    auto mesh = makeCube(1);
    mesh.constrainNodes({0, 3}, kDofTranslations);
    mesh.constrainNode(1, DofUy | DofUz, {0, 0.25, 0.25, 0, 0, 0});
    mesh.constrainNode(2, DofUx | DofRx | DofRy, {1.5, 0, 0, 0, 0, 0});
    const auto written = AbaqusInpWriter::writeFile((dir / "rt.inp").string(), mesh);
    CHECK(written.success);
    CHECK(written.constraintRecordsWritten == 1 + 1 + 1 + 2);   // nodes 0, 3 and 1: one run each; node 2: ux, then rx..ry
    AbaqusInpImporter importer;
    MeshData back;
    CHECK(importer.read(dir / "rt.inp", back).status == ImportStatus::Success);
    CHECK(back.constrainedNodeCount() == 4);
    bool same = true;
    for (const auto& entry : mesh.getConstraints()) {
        const auto* c = back.constraintOf(entry.first);
        same = same && c && c->dofs == entry.second.dofs && c->values == entry.second.values;
    }
    CHECK(same);
}

void testSupports() {
    auto mesh = makeCube(2);                           // 27 nodes, x = 0 plane has 9
    const auto parsed = parseAnalysisScript("material E=210000 nu=0.3 rho=7.85e-9\nfix plane:x=0\nfix plane:y=0 dof=3\nfix plane:x=2 dof=1 value=0.1,0,0\nforce plane:x=2 F=1,0,0\nstep static\n");
    CHECK(parsed.success);
    std::vector<ImportDiagnostic> diagnostics;
    const auto constraints = supportConstraints(mesh, parsed.model, diagnostics);
    CHECK(diagnostics.empty());
    std::size_t xzero = 0, bothDofs = 0;
    for (const auto& c : constraints) {
        const auto p = mesh.nodeCoordinates(c.node);
        if (p[0] == 0.0) ++xzero;
        if (p[0] == 0.0 && p[1] == 0.0) { ++bothDofs; CHECK(c.dofs == kDofTranslations); }   // x=0 fixes all three, y=0 adds z again
        if (p[0] == 2.0) { CHECK((c.dofs & DofUx) != 0); CHECK(c.values[0] == 0.1); }
    }
    CHECK(xzero == 9 && bothDofs == 3);
    for (std::size_t i = 1; i < constraints.size(); ++i) CHECK(constraints[i - 1].node < constraints[i].node);

    MeshData applied = makeCube(2);
    applySupports(applied, parsed.model, diagnostics);
    CHECK(applied.constrainedNodeCount() == constraints.size());
    CHECK(applied.constrainedDofCount() >= 9 * 3 + 9);

    // A conflicting value and an empty selection are reported, and the offending support is left out.
    const auto conflict = parseAnalysisScript("material E=1 nu=0.3 rho=1\nfix plane:x=0 dof=1\nfix plane:x=0 dof=1 value=1,0,0\nfix plane:z=50\nstep static\n");
    CHECK(conflict.success);
    diagnostics.clear();
    const auto some = supportConstraints(mesh, conflict.model, diagnostics);
    CHECK(diagnostics.size() == 2);
    CHECK(some.size() == 9);
    for (const auto& c : some) CHECK(c.dofs == DofUx && c.values[0] == 0.0);
}

void testWebView() {
    auto mesh = makeCube(2);
    mesh.constrainNodes({0, 1, 2}, kDofTranslations);
    mesh.constrainNode(13, DofUx);                      // the centre node: on no face, so it is not drawn
    mesh.constrainNode(26, DofRz, {0, 0, 0, 0, 0, 0.5});
    std::vector<unsigned char> image;
    WebViewOptions options;
    options.includeInteriorFaces = false;               // so that the centre node is on no exported face
    auto result = WebViewWriter::build(mesh, options, image);
    CHECK(result.success);
    CHECK(result.constrainedNodes == 5 && result.constraintVertices == 4);
    const std::string text(image.begin(), image.end());
    CHECK(text.find("\"constraints\":{") != std::string::npos);
    CHECK(text.find("\"constrainedNodes\":5") != std::string::npos && text.find("\"constraintVertices\":4") != std::string::npos);

    // Words of the chunk: pairs (vertex, mask | 256 for a prescribed value).
    std::uint32_t jsonLength = 0, payloadLength = 0;
    std::memcpy(&jsonLength, image.data() + 12, 4);
    std::memcpy(&payloadLength, image.data() + 16, 4);
    const auto key = text.find("\"constraints\":{\"offset\":");
    CHECK(key != std::string::npos);
    const std::size_t offset = std::stoull(text.substr(key + 24));
    const auto countKey = text.find("\"count\":", key);
    const std::size_t count = std::stoull(text.substr(countKey + 8));
    CHECK(count == 8);
    const unsigned char* words = image.data() + 24 + jsonLength + offset;
    std::uint32_t w[8];
    std::memcpy(w, words, sizeof w);
    CHECK(w[1] == kDofTranslations && w[3] == kDofTranslations && w[5] == kDofTranslations);
    CHECK(w[7] == (DofRz | 256u));
    CHECK(w[0] < w[2] && w[2] < w[4] && w[4] < w[6]);   // vertices follow the node order

    options.includeConstraints = false;
    result = WebViewWriter::build(mesh, options, image);
    CHECK(result.success && result.constraintVertices == 0);
    CHECK(std::string(image.begin(), image.end()).find("\"constraints\":{") == std::string::npos);

    options.includeConstraints = true;
    NodeConstraint extra;
    extra.node = 8; extra.dofs = DofUy;
    options.extraConstraints.push_back(extra);
    options.extraConstraints.push_back({0, DofRx, {}});   // merged into node 0
    result = WebViewWriter::build(mesh, options, image);
    CHECK(result.success && result.constrainedNodes == 6 && result.constraintVertices == 5);
    options.extraConstraints.push_back({999, DofUx, {}});
    CHECK(!WebViewWriter::build(mesh, options, image).success);
}

std::string slurpText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/** The node IDs listed under `*NSET, NSET=name` of an Abaqus deck. */
std::set<std::uint64_t> nsetIds(const std::string& deck, const std::string& name) {
    std::set<std::uint64_t> ids;
    const auto at = deck.find("*NSET, NSET=" + name + "\n");
    if (at == std::string::npos) return ids;
    std::istringstream in(deck.substr(at));
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line) && !line.empty() && line[0] != '*') {
        std::istringstream fields(line);
        std::string item;
        while (std::getline(fields, item, ',')) ids.insert(std::stoull(item));
    }
    return ids;
}

void testSelectedNodes() {
    const auto mesh = makeCube(2);                     // node id = 100 + (z * 3 + y) * 3 + x
    std::string error;
    ViewerSelection byNode;
    byNode.mode = "node"; byNode.nodes = {105, 100, 105};
    auto nodes = selectedNodes(mesh, byNode, error);
    CHECK(error.empty() && nodes == std::vector<NodeIndex>({0, 5}));               // sorted, without repetitions

    ViewerSelection byElement;
    byElement.mode = "element"; byElement.elements = {1};
    nodes = selectedNodes(mesh, byElement, error);
    CHECK(error.empty() && nodes.size() == 8);

    ViewerSelection byFace;                            // the bottom face of the element 1: nodes 0, 1, 4 (and 3) of its z = 0 square
    byFace.mode = "face"; byFace.faces.push_back({1, {{100, 101, 104}}});
    nodes = selectedNodes(mesh, byFace, error);
    CHECK(error.empty() && nodes == std::vector<NodeIndex>({0, 1, 3, 4}));
    byFace.faces.push_back({2, {{101, 102, 105}}});    // the bottom face of the element 2 shares the nodes 1 and 4
    nodes = selectedNodes(mesh, byFace, error);
    CHECK(error.empty() && nodes == std::vector<NodeIndex>({0, 1, 2, 3, 4, 5}));

    ViewerSelection bad;
    CHECK(selectedNodes(mesh, bad, error).empty() && !error.empty());              // no mode
    bad.mode = "node";
    CHECK(selectedNodes(mesh, bad, error).empty() && error == "The selection is empty");
    bad.nodes = {100, 999};
    CHECK(selectedNodes(mesh, bad, error).empty() && error.find("999") != std::string::npos);
    bad.mode = "face"; bad.nodes.clear(); bad.faces.push_back({1, {{100, 101, 126}}});   // 126 is not a node of the element
    CHECK(selectedNodes(mesh, bad, error).empty() && !error.empty());
}

void testSolverInputs() {
    const auto dir = tempDir();
    auto mesh = makeCube(2);
    std::vector<NodeIndex> plane;
    for (NodeIndex i = 0; i < mesh.getNumNodes(); ++i) if (mesh.nodeCoordinates(i)[0] == 0.0) plane.push_back(i);
    CHECK(plane.size() == 9);
    const std::string common = "material E=210000 nu=0.3 rho=7.85e-9\nforce plane:x=2 F=1,0,0\nstep static\n";

    // A constraint of the mesh gives the deck of the same `fix` written in the script.
    auto viaMesh = makeCube(2);
    viaMesh.constrainNodes(plane, DofUy | DofUx);
    const auto parsedPlain = parseAnalysisScript(common);
    CHECK(parsedPlain.success);
    const auto meshResult = writeSolverInput(viaMesh, parsedPlain.model, dir / "mesh.inp");
    CHECK(meshResult.success);
    const auto parsedFix = parseAnalysisScript(common + "fix plane:x=0 dof=1,2\n");
    CHECK(parsedFix.success);
    const auto scriptResult = writeSolverInput(makeCube(2), parsedFix.model, dir / "script.inp");
    CHECK(scriptResult.success);
    const auto meshDeck = slurpText(dir / "mesh.inp"), scriptDeck = slurpText(dir / "script.inp");
    CHECK(!nsetIds(meshDeck, "MG_FIX1").empty() && nsetIds(meshDeck, "MG_FIX1") == nsetIds(scriptDeck, "MG_FIX1"));
    CHECK(nsetIds(meshDeck, "MG_FIX1").size() == 9);
    for (const char* line : {"MG_FIX1, 1, 1, 0\n", "MG_FIX1, 2, 2, 0\n"}) CHECK(meshDeck.find(line) != std::string::npos);
    CHECK(meshDeck.find("MG_FIX1, 3, 3") == std::string::npos);                       // uz stays free
    bool info = false;
    for (const auto& d : meshResult.diagnostics) info = info || d.code == "CONSTRAINT_FROM_MESH";
    CHECK(info);

    // Different prescribed values make different supports; they come after the ones of the script; rotations are reported and dropped.
    auto mixed = makeCube(2);
    mixed.constrainNodes({plane[0], plane[1]}, DofUx, {0.5, 0, 0, 0, 0, 0});
    mixed.constrainNodes({plane[2]}, DofUx | DofRz, {0.0, 0, 0, 0, 0, 0.1});
    const auto withFix = parseAnalysisScript(common + "fix plane:x=2 dof=3\n");
    const auto mixedResult = writeSolverInput(mixed, withFix.model, dir / "mixed.inp");
    CHECK(mixedResult.success);
    const auto mixedDeck = slurpText(dir / "mixed.inp");
    CHECK(nsetIds(mixedDeck, "MG_FIX1").size() == 9 && nsetIds(mixedDeck, "MG_FIX2").size() == 1 && nsetIds(mixedDeck, "MG_FIX3").size() == 2);
    CHECK(mixedDeck.find("MG_FIX3, 1, 1, 0.5") != std::string::npos || mixedDeck.find("MG_FIX2, 1, 1, 0.5") != std::string::npos);
    bool rotation = false;
    for (const auto& d : mixedResult.diagnostics) rotation = rotation || (d.code == "CONSTRAINT_ROTATION_IGNORED" && d.level == ImportDiagnosticLevel::Warning);
    CHECK(rotation);
    CHECK(mixedDeck.find("MG_FIX2, 6") == std::string::npos && mixedDeck.find("MG_FIX3, 6") == std::string::npos);

    // A conflict between the mesh and the script is refused, as two `fix` that disagree are.
    auto conflicting = makeCube(2);
    conflicting.constrainNodes({plane[0]}, DofUx, {0.5, 0, 0, 0, 0, 0});
    const auto conflictModel = parseAnalysisScript(common + "fix plane:x=0 dof=1\n");
    CHECK(!writeSolverInput(conflicting, conflictModel.model, dir / "conflict.inp").success);

    // A mesh with no constraint writes what it always did.
    const auto noneResult = writeSolverInput(makeCube(2), parsedFix.model, dir / "none.inp");
    CHECK(noneResult.success && slurpText(dir / "none.inp") == scriptDeck);

    // code_aster: the same support, as a group of nodes of the case.
    CodeAsterOptions options;
    const auto aster = writeCodeAsterCase(viaMesh, parsedPlain.model, dir / "aster", options);
    CHECK(aster.success);
    std::string all;
    for (const auto& entry : fs::recursive_directory_iterator(dir / "aster")) if (entry.is_regular_file()) all += slurpText(entry.path());
    CHECK(all.find("OM_FIX1") != std::string::npos && all.find("DX=0") != std::string::npos && all.find("DY=0") != std::string::npos);
    CHECK(all.find("DZ=") == std::string::npos);
}

} // namespace

int main() {
    testApi();
    testMgom();
    testInpImport();
    testInpRoundTrip();
    testSupports();
    testWebView();
    testSelectedNodes();
    testSolverInputs();
    if (g_failures) { std::cerr << g_failures << " check(s) failed\n"; return 1; }
    std::cout << "mesh_constraints_test: all passed\n";
    return 0;
}
