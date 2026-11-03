/**
 * @file analysis_setup_test.cpp
 * @brief Tests of the analysis script parser, node selectors, CalculiX input writer and the process runner.
 */
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/ProcessRunner.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

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

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

bool contains(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

bool hasCode(const std::vector<ImportDiagnostic>& d, const std::string& code, std::size_t line = 0) {
    for (const auto& x : d)
        if (x.code == code && (line == 0 || x.line == line)) return true;
    return false;
}

/** Unit hexahedron, node ids 11..18 (standard order), quads on z=0, z=1 and x=1 as surface groups. */
MeshData makeHex() {
    MeshData m;
    const double c[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    for (int i = 0; i < 8; ++i) m.addNode(11 + i, c[i][0], c[i][1], c[i][2]);
    m.addElement("solid", ElementType::Hexahedron8, 1, {0, 1, 2, 3, 4, 5, 6, 7});
    m.addElement("faces", ElementType::Quadrilateral4, 101, {0, 3, 2, 1});   // z = 0 (reversed winding on purpose)
    m.addElement("faces", ElementType::Quadrilateral4, 102, {4, 5, 6, 7});   // z = 1
    m.addElement("faces", ElementType::Quadrilateral4, 103, {1, 2, 6, 5});   // x = 1
    m.addPhysicalGroup("Bottom", 2, 1, {101});
    m.addPhysicalGroup("Top", 2, 2, {102});
    m.addPhysicalGroup("Right", 2, 3, {103});
    m.addPhysicalGroup("Body", 3, 4, {1});
    return m;
}

/** One tetrahedron (0,0,0) (1,0,0) (0,1,0) (0,0,1) with its four faces as separate surface groups. */
MeshData makeTet() {
    MeshData m;
    m.addNode(1, 0, 0, 0);
    m.addNode(2, 1, 0, 0);
    m.addNode(3, 0, 1, 0);
    m.addNode(4, 0, 0, 1);
    m.addElement("solid", ElementType::Tetrahedron4, 1, {0, 1, 2, 3});
    m.addElement("faces", ElementType::Triangle3, 11, {0, 1, 2});   // z = 0     -> S1
    m.addElement("faces", ElementType::Triangle3, 12, {0, 1, 3});   // y = 0     -> S2
    m.addElement("faces", ElementType::Triangle3, 13, {1, 2, 3});   // slanted   -> S3
    m.addElement("faces", ElementType::Triangle3, 14, {0, 2, 3});   // x = 0     -> S4
    m.addPhysicalGroup("F1", 2, 1, {11});
    m.addPhysicalGroup("F2", 2, 2, {12});
    m.addPhysicalGroup("F3", 2, 3, {13});
    m.addPhysicalGroup("F4", 2, 4, {14});
    return m;
}

AnalysisModel parseOk(const std::string& text) {
    const auto r = parseAnalysisScript(text);
    CHECK(r.success);
    return r.model;
}

fs::path tempDir() {
    const fs::path d = fs::temp_directory_path() / ("mg_analysis_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(d);
    return d;
}

void testParser() {
    const auto model = parseOk(
        "# comment\n"
        "material E=210000 nu=0.3 rho=7.85e-9 name=steel   # trailing\n"
        "fix plane:x=0\n"
        "fix group:Bottom dof=3\n"
        "force plane:x=10 F=0,-100,0\r\n"
        "pressure group:Top p=2.5\n"
        "gravity g=0,0,-9810\n"
        "step frequency modes=4\n"
        "output element=off\n");
    CHECK(model.material.name == "steel");
    CHECK(model.material.youngModulus == 210000.0 && model.material.poisson == 0.3 && model.material.density == 7.85e-9);
    CHECK(model.supports.size() == 2);
    CHECK(model.supports[0].where.kind == SelectorKind::Plane && model.supports[0].where.axis == 0 && model.supports[0].where.position == 0.0);
    CHECK(model.supports[0].dof[0] && model.supports[0].dof[1] && model.supports[0].dof[2]);
    CHECK(model.supports[1].where.kind == SelectorKind::Group && model.supports[1].where.name == "Bottom");
    CHECK(!model.supports[1].dof[0] && !model.supports[1].dof[1] && model.supports[1].dof[2]);
    CHECK(model.forces.size() == 1 && model.forces[0].total[1] == -100.0);
    CHECK(model.pressures.size() == 1 && model.pressures[0].group == "Top" && model.pressures[0].pressure == 2.5);
    CHECK(model.hasGravity && model.gravity[2] == -9810.0);
    CHECK(model.step == StepType::Frequency && model.modes == 4);
    CHECK(model.elementPrint == 0);

    // Errors: every one reported with its own line number, none stops the parsing of the others.
    const auto bad = parseAnalysisScript(
        "material E=abc nu=0.3\n"                  // 1 bad number
        "fix plane:q=0\n"                          // 2 bad selector
        "force all F=1,2\n"                        // 3 bad vector
        "frobnicate all\n"                         // 4 unknown command
        "fix all dof=4\n"                          // 5 bad dof
        "output bogus=1\n"                         // 6 unknown option
        "pressure all p=1\n"                       // 7 needs group
        "step frequency modes=0\n");               // 8 bad modes
    CHECK(!bad.success);
    CHECK(hasCode(bad.diagnostics, "SCRIPT_BAD_NUMBER", 1));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_SELECTOR", 2));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_BAD_VECTOR", 3));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_UNKNOWN_COMMAND", 4));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_BAD_DOF", 5));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_UNKNOWN_KEY", 6));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_SELECTOR", 7));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_BAD_MODES", 8));
    CHECK(hasCode(bad.diagnostics, "SCRIPT_NO_MATERIAL"));

    CHECK(!parseAnalysisScript("").success);
    CHECK(!parseAnalysisScript("material E=1 nu=0.3\nmaterial E=2 nu=0.3\n").success);
    CHECK(!parseAnalysisScript("material E=1 nu=0.3 E=2\n").success);
    CHECK(parseAnalysisScript(exampleAnalysisScript()).success);
    CHECK(parseAnalysisScript("material E=1 nu=0.3\n").success);   // default step: warning only
}

void testSelectors() {
    const MeshData hex = makeHex();
    std::string err;
    NodeSelector s;
    s.kind = SelectorKind::Plane; s.axis = 2; s.position = 1.0;
    auto n = selectNodes(hex, s, err);
    CHECK(n.size() == 4 && err.empty());
    for (const auto i : n) CHECK(hex.nodeId(i) >= 15);
    s.position = 0.5;
    n = selectNodes(hex, s, err);
    CHECK(n.empty() && contains(err, "No node lies on the plane z = 0.5"));
    s.position = 0.6; s.tolerance = 0.2;
    CHECK(selectNodes(hex, s, err).size() == 0);
    s.position = 0.9; s.tolerance = 0.2;
    CHECK(selectNodes(hex, s, err).size() == 4);

    NodeSelector g;
    g.kind = SelectorKind::Group; g.name = "right";            // case-insensitive
    n = selectNodes(hex, g, err);
    CHECK(n.size() == 4);
    g.name = "Body";
    CHECK(selectNodes(hex, g, err).size() == 8);               // volume group: all its nodes
    g.name = "Nope";
    n = selectNodes(hex, g, err);
    CHECK(n.empty() && contains(err, "Unknown group") && contains(err, "Bottom"));
    NodeSelector all;
    CHECK(selectNodes(hex, all, err).size() == 8);

    MeshData withCadSet = hex;
    withCadSet.addNodeSet("BladeRoot_NODES", {0, 3, 4, 7, 0, 3});
    NodeSelector cad;
    cad.kind = SelectorKind::Group;
    cad.name = "BladeRoot_NODES";
    n = selectNodes(withCadSet, cad, err);
    CHECK(err.empty() && n.size() == 4);
    CHECK(n[0] == 0 && n[1] == 3 && n[2] == 4 && n[3] == 7);

    MeshData genericGroups = hex;
    genericGroups.addMeshGroup({"CadRoot", 2, MeshGroupSource::CAD, {17}, {}, {0, 3, 4, 7}});
    genericGroups.addMeshGroup({"CadFace", 2, MeshGroupSource::CAD, {18}, {101}, {}});
    cad.name = "cadroot";
    n = selectNodes(genericGroups, cad, err);
    CHECK(err.empty() && n == std::vector<NodeIndex>({0, 3, 4, 7}));
    cad.name = "CadFace";
    n = selectNodes(genericGroups, cad, err);
    CHECK(err.empty() && n == std::vector<NodeIndex>({0, 1, 2, 3}));
}

void testWriterStatic() {
    const fs::path dir = tempDir();
    MeshData hex = makeHex();
    hex.addNodeSet("BladeRoot_NODES", {0, 3, 4, 7});
    auto model = parseOk("material E=210000 nu=0.3 rho=7.85e-9\n"
                         "fix group:Bottom\n"
                         "fix group:BladeRoot_NODES dof=1,2,3\n"
                         "fix plane:x=0 dof=1\n"
                         "force group:Top F=0,0,-40\n"
                         "pressure group:Right p=2\n"
                         "pressure group:Bottom p=3\n"
                         "pressure group:Top p=4\n"
                         "gravity g=0,0,-10\n");
    const auto r = writeSolverInput(hex, model, dir / "job.inp");
    CHECK(r.success);
    if (!r.success) std::cerr << r.error << "\n";
    CHECK(r.elements == 1 && r.nodes == 8);
    CHECK(r.fixedNodes == 6);            // bottom 4 + x=0 face (4), sharing 2 nodes
    CHECK(r.loadedNodes == 4 && r.pressureFaces == 3);
    const std::string t = slurp(dir / "job.inp");
    CHECK(contains(t, "*ELEMENT, TYPE=C3D8"));
    CHECK(!contains(t, "TYPE=S4"));                      // surface elements are not structure
    CHECK(contains(t, "*MATERIAL, NAME=MATERIAL"));
    CHECK(contains(t, "*ELASTIC\n210000, 0.3"));
    CHECK(contains(t, "*DENSITY\n7.85e-09"));
    CHECK(contains(t, "*SOLID SECTION, ELSET=SOLID, MATERIAL=MATERIAL"));
    CHECK(contains(t, "*NSET, NSET=MG_FIX1\n11, 12, 13, 14\n"));
    CHECK(contains(t, "*NSET, NSET=BLADEROOT_NODES\n11, 14, 15, 18\n"));
    CHECK(contains(t, "MG_FIX1, 1, 1, 0.") && contains(t, "MG_FIX1, 3, 3, 0.") &&
          contains(t, "MG_FIX2, 1, 1, 0.") && contains(t, "MG_FIX2, 3, 3, 0.") &&
          contains(t, "MG_FIX3, 1, 1, 0."));
    CHECK(!contains(t, "MG_FIX3, 2, 2") && !contains(t, "MG_FIX3, 3, 3"));
    CHECK(contains(t, "*STATIC"));
    CHECK(contains(t, "MG_LOAD1, 3, -10"));              // -40 shared by 4 nodes
    // Independent derivation of the CalculiX face numbers (P1 = nodes 1-2-3-4, P2 = 5-6-7-8, P4 = 2-3-7-6):
    CHECK(contains(t, "1, P4, 2"));                      // x = 1
    CHECK(contains(t, "1, P1, 3"));                      // z = 0
    CHECK(contains(t, "1, P2, 4"));                      // z = 1
    CHECK(contains(t, "MG_EALL, GRAV, 10, 0, 0, -1"));
    CHECK(contains(t, "*NODE FILE\nU, S, E"));
    CHECK(contains(t, "*EL PRINT, ELSET=MG_EALL\nS, E"));
    CHECK(t.find("*END STEP") != std::string::npos);
    // Order: the keyword data of the step comes after *STEP.
    CHECK(t.find("*STEP") < t.find("*STATIC") && t.find("*STATIC") < t.find("*NODE FILE"));

    model.elementPrint = 0;
    const auto r2 = writeSolverInput(hex, model, dir / "noprint.inp");
    CHECK(r2.success && !contains(slurp(dir / "noprint.inp"), "*EL PRINT"));
    CHECK(hasCode(r2.diagnostics, "ANALYSIS_NO_ELEMENT_PRINT"));
    fs::remove_all(dir);
}

void testTetFaces() {
    const fs::path dir = tempDir();
    const MeshData tet = makeTet();
    const auto model = parseOk("material E=1000 nu=0.25\nfix plane:z=0\n"
                               "pressure group:F1 p=1\npressure group:F2 p=2\npressure group:F3 p=3\npressure group:F4 p=4\n");
    const auto r = writeSolverInput(tet, model, dir / "tet.inp");
    CHECK(r.success && r.pressureFaces == 4);
    const std::string t = slurp(dir / "tet.inp");
    CHECK(contains(t, "1, P1, 1\n"));    // z = 0 : 1-2-3
    CHECK(contains(t, "1, P2, 2\n"));    // y = 0 : 1-4-2
    CHECK(contains(t, "1, P3, 3\n"));    // slanted : 2-4-3
    CHECK(contains(t, "1, P4, 4\n"));    // x = 0 : 3-4-1
    CHECK(contains(t, "*ELEMENT, TYPE=C3D4"));

    MeshData genericTet = tet;
    genericTet.addMeshGroup({"CAD_F1", 2, MeshGroupSource::CAD, {51}, {11}, {0, 1, 2}});
    const auto genericModel = parseOk("material E=1000 nu=0.25\nfix plane:z=0\npressure group:CAD_F1 p=7\n");
    const auto genericResult = writeSolverInput(genericTet, genericModel, dir / "generic-face.inp");
    CHECK(genericResult.success && genericResult.pressureFaces == 1);
    CHECK(contains(slurp(dir / "generic-face.inp"), "1, P1, 7"));
    fs::remove_all(dir);
}

void testWriterFrequencyAndValidation() {
    const fs::path dir = tempDir();
    const MeshData hex = makeHex();
    auto f = parseOk("material E=210000 nu=0.3 rho=7.85e-9\nfix group:Bottom\nstep frequency modes=3\nforce all F=1,0,0\n");
    const auto r = writeSolverInput(hex, f, dir / "f.inp");
    CHECK(r.success && hasCode(r.diagnostics, "ANALYSIS_LOADS_IGNORED"));
    const std::string t = slurp(dir / "f.inp");
    CHECK(contains(t, "*FREQUENCY\n3\n") && contains(t, "*NODE FILE\nU\n") && !contains(t, "*CLOAD") && !contains(t, "*EL PRINT"));

    auto noDensity = parseOk("material E=1 nu=0.3\nfix all\nstep frequency\n");
    CHECK(!writeSolverInput(hex, noDensity, dir / "x.inp").success);
    auto noSupport = parseOk("material E=1 nu=0.3\nforce all F=1,0,0\n");
    auto rs = writeSolverInput(hex, noSupport, dir / "x.inp");
    CHECK(!rs.success && contains(rs.error, "support"));
    auto badNu = parseOk("material E=1 nu=0.5\nfix all\n");
    CHECK(!writeSolverInput(hex, badNu, dir / "x.inp").success);
    auto emptyPlane = parseOk("material E=1 nu=0.3\nfix plane:x=7\n");
    auto re = writeSolverInput(hex, emptyPlane, dir / "x.inp");
    CHECK(!re.success && contains(re.error, "fix #1") && contains(re.error, "No node lies"));
    auto notSurface = parseOk("material E=1 nu=0.3\nfix all\npressure group:Body p=1\n");
    CHECK(contains(writeSolverInput(hex, notSurface, dir / "x.inp").error, "not a surface group"));
    auto unknownGroup = parseOk("material E=1 nu=0.3\nfix all\npressure group:Zzz p=1\n");
    CHECK(contains(writeSolverInput(hex, unknownGroup, dir / "x.inp").error, "unknown surface group"));
    auto noLoad = parseOk("material E=1 nu=0.3\nfix all\n");
    CHECK(hasCode(writeSolverInput(hex, noLoad, dir / "x.inp").diagnostics, "ANALYSIS_NO_LOAD"));

    MeshData surfaceOnly;
    surfaceOnly.addNode(1, 0, 0, 0); surfaceOnly.addNode(2, 1, 0, 0); surfaceOnly.addNode(3, 0, 1, 0);
    surfaceOnly.addElement("s", ElementType::Triangle3, 1, {0, 1, 2});
    auto rn = writeSolverInput(surfaceOnly, noLoad, dir / "x.inp");
    CHECK(!rn.success && contains(rn.error, "no volume elements"));

    // A face shared by two volume elements is not a boundary: two tetrahedra glued on 1-2-3.
    MeshData glued = makeTet();
    glued.addNode(5, 1, 1, -1);
    glued.addElement("solid", ElementType::Tetrahedron4, 2, {0, 1, 2, 4});
    auto sharedModel = parseOk("material E=1 nu=0.3\nfix all\npressure group:F1 p=1\n");
    CHECK(contains(writeSolverInput(glued, sharedModel, dir / "x.inp").error, "between two volume elements"));
    fs::remove_all(dir);
}

void testRunner() {
#ifndef _WIN32
    ProcessRunner r;
    CHECK(r.start({"/bin/sh", "-c", "echo one; echo two >&2; printf partial"}, ".").empty());
    r.wait();
    auto s = r.poll();
    CHECK(s.state == ProcessState::Finished && s.exitCode == 0);
    CHECK(s.newLines.size() == 3 && s.newLines[0] == "one" && s.newLines[1] == "two" && s.newLines[2] == "partial");
    CHECK(r.poll().newLines.empty());                                            // lines are delivered once

    CHECK(r.start({"/bin/sh", "-c", "echo bad; exit 3"}, ".").empty());
    r.wait();
    s = r.poll();
    CHECK(s.state == ProcessState::Failed && s.exitCode == 3);

    const auto missing = r.start({"/nonexistent/solver-xyz"}, ".");
    CHECK(contains(missing, "Cannot start"));
    CHECK(contains(r.start({"/bin/true"}, "/nonexistent-dir"), "Cannot start"));

    // Cancel a process that ignores nothing, and one that ignores SIGTERM (needs the SIGKILL escalation).
    CHECK(r.start({"/bin/sh", "-c", "sleep 30"}, ".").empty());
    CHECK(!r.start({"/bin/true"}, ".").empty());                                 // already running
    const auto t0 = std::chrono::steady_clock::now();
    r.cancel(1.0);
    r.wait();
    CHECK(r.poll().state == ProcessState::Cancelled);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0);

    CHECK(r.start({"/bin/sh", "-c", "trap '' TERM; echo ready; while :; do sleep 1; done"}, ".").empty());
    for (int i = 0; i < 100 && r.poll().totalLines == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto t1 = std::chrono::steady_clock::now();
    r.cancel(0.5);
    r.wait();
    s = r.poll();
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    CHECK(s.state == ProcessState::Cancelled && s.exitCode == 128 + 9);
    CHECK(took >= 0.4 && took < 4.0);
#endif
}

} // namespace

int main() {
    testParser();
    testSelectors();
    testWriterStatic();
    testTetFaces();
    testWriterFrequencyAndValidation();
    testRunner();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "analysis_setup_test: all checks passed\n";
    return 0;
}
