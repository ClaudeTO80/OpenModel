/**
 * @file analysis_setup_test.cpp
 * @brief Tests of the analysis script parser, node selectors, CalculiX input writer and the process runner.
 */
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/CodeAster.hpp>
#include <OpenModel/ProcessRunner.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

bool containsNsetMembers(const std::string& text, const std::string& name,
                         std::vector<int> expected) {
    const std::string header = "*NSET, NSET=" + name;
    const auto headerPos = text.find(header);
    if (headerPos == std::string::npos) return false;
    const auto dataStart = text.find('\n', headerPos);
    if (dataStart == std::string::npos) return false;
    const auto nextKeyword = text.find("\n*", dataStart + 1);
    std::string data = text.substr(dataStart + 1,
        nextKeyword == std::string::npos ? std::string::npos : nextKeyword - dataStart - 1);
    for (char& c : data) if (c == ',') c = ' ';
    std::istringstream input(data);
    std::vector<int> actual;
    int id = 0;
    while (input >> id) actual.push_back(id);
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    return actual == expected;
}

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

void testStableConditionIds() {
    const auto parsed = parseAnalysisScript(
        "material id=steel-main E=210000 nu=0.3 rho=7.85e-9 name=steel\n"
        "fix id=clamp-west group:CLAMP dof=1,2,3 value=0,0,0\n"
        "fix group:SECONDARY dof=3 value=0,0,-0.25\n"
        "force id=tip-load plane:x=10 F=0,0,-100 tol=1e-8\n"
        "pressure id=top-pressure group:TOP p=2.5\n"
        "gravity g=0,0,-9.81\n"
        "step frequency modes=8\n");
    CHECK(parsed.success);
    CHECK(parsed.model.material.id == "steel-main");
    CHECK(parsed.model.supports.size() == 2);
    CHECK(parsed.model.supports[0].id == "clamp-west");
    CHECK(parsed.model.supports[1].id == "fix-1");
    CHECK(parsed.model.forces.size() == 1 && parsed.model.forces[0].id == "tip-load");
    CHECK(parsed.model.pressures.size() == 1 && parsed.model.pressures[0].id == "top-pressure");

    const std::string serialized = serializeAnalysisScript(parsed.model);
    const auto roundTrip = parseAnalysisScript(serialized);
    CHECK(roundTrip.success);
    CHECK(roundTrip.model.material.id == "steel-main");
    CHECK(roundTrip.model.supports.size() == 2);
    CHECK(roundTrip.model.supports[0].id == "clamp-west");
    CHECK(roundTrip.model.supports[1].id == "fix-1");
    CHECK(roundTrip.model.supports[1].displacement[2] == -0.25);
    CHECK(roundTrip.model.forces.size() == 1 && roundTrip.model.forces[0].id == "tip-load");
    CHECK(roundTrip.model.forces[0].where.tolerance == 1e-8);
    CHECK(roundTrip.model.pressures.size() == 1 && roundTrip.model.pressures[0].id == "top-pressure");
    CHECK(roundTrip.model.hasGravity && roundTrip.model.gravity[2] == -9.81);
    CHECK(roundTrip.model.jobs.front().steps.front().type == StepType::Frequency && roundTrip.model.jobs.front().steps.front().modes == 8);

    const auto duplicate = parseAnalysisScript(
        "material id=shared E=1 nu=0.3\n"
        "fix id=shared all\n");
    CHECK(!duplicate.success && hasCode(duplicate.diagnostics, "SCRIPT_DUPLICATE_ID"));
    const auto invalid = parseAnalysisScript("material id=bad/id E=1 nu=0.3\n");
    CHECK(!invalid.success && hasCode(invalid.diagnostics, "SCRIPT_BAD_ID"));
    const auto generatedCollision = parseAnalysisScript(
        "material E=1 nu=0.3\n"
        "fix id=fix-1 all\n"
        "fix all\n");
    CHECK(generatedCollision.success);
    CHECK(generatedCollision.model.supports[0].id == "fix-1");
    CHECK(generatedCollision.model.supports[1].id == "fix-2");
}


void testConditionCrudById() {
    auto parsed = parseAnalysisScript(
        "material id=steel E=210000 nu=0.3\n"
        "fix id=clamp all\n"
        "force id=load all F=1,2,3\n"
        "pressure id=pressure group:TOP p=4\n");
    CHECK(parsed.success);
    auto& model = parsed.model;
    CHECK(findSupport(model, "clamp") != nullptr);
    CHECK(findForce(model, "load") != nullptr);
    CHECK(findPressure(model, "pressure") != nullptr);
    CHECK(findSupport(model, "missing") == nullptr);

    std::string error;
    FixedSupport support;
    support.where.kind = SelectorKind::Plane;
    support.where.axis = 2;
    support.where.position = 5.0;
    support.id = "attempted-id-change";
    CHECK(updateSupport(model, "clamp", support, error));
    CHECK(findSupport(model, "clamp") != nullptr);
    CHECK(findSupport(model, "clamp")->where.position == 5.0);
    CHECK(findSupport(model, "attempted-id-change") == nullptr);
    CHECK(!updateSupport(model, "absent", support, error));

    NodalForce force;
    force.total = {{10.0, 0.0, -2.0}};
    CHECK(updateForce(model, "load", force, error));
    CHECK(findForce(model, "load")->total[0] == 10.0);
    CHECK(eraseForce(model, "load"));
    CHECK(!eraseForce(model, "load"));

    SurfacePressure pressure;
    pressure.group = "TOP";
    pressure.pressure = 12.0;
    CHECK(updatePressure(model, "pressure", pressure, error));
    CHECK(findPressure(model, "pressure")->pressure == 12.0);
    CHECK(erasePressure(model, "pressure"));

    AnalysisMaterial material;
    material.name = "ALUMINIUM";
    material.youngModulus = 70000.0;
    material.poisson = 0.33;
    CHECK(updateMaterial(model, "steel", material, error));
    CHECK(model.material.id == "steel" && model.material.name == "ALUMINIUM");
    CHECK(!updateMaterial(model, "missing", material, error));
    CHECK(eraseSupport(model, "clamp"));
    CHECK(serializeAnalysisScript(model).find("id=steel") != std::string::npos);
}

void testParser() {
    const auto model = parseOk(
        "# comment\n"
        "material E=210000 nu=0.3 rho=7.85e-9 name=steel   # trailing\n"
        "fix plane:x=0\n"
        "fix group:Bottom dof=3 value=0,0,-2.5\n"
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
    CHECK(model.supports[1].displacement[2] == -2.5);
    CHECK(model.forces.size() == 1 && model.forces[0].total[1] == -100.0);
    CHECK(model.pressures.size() == 1 && model.pressures[0].group == "Top" && model.pressures[0].pressure == 2.5);
    CHECK(model.hasGravity && model.gravity[2] == -9810.0);
    CHECK(model.jobs.front().steps.front().type == StepType::Frequency && model.jobs.front().steps.front().modes == 4);
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
    CHECK(parseAnalysisScript("material E=1 nu=0.3\nmaterial E=2 nu=0.3\n").success);   // further materials are allowed
    CHECK(!parseAnalysisScript("material E=1 nu=0.3\nmaterial id=m E=2 nu=0.3\nmaterial id=m E=3 nu=0.3\n").success);
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
    CHECK(containsNsetMembers(t, "BLADEROOT_NODES", {11, 14, 15, 18}));
    CHECK(contains(t, "MG_FIX1, 1, 1, 0\n") && contains(t, "MG_FIX1, 3, 3, 0\n") &&
          contains(t, "MG_FIX2, 1, 1, 0\n") && contains(t, "MG_FIX2, 3, 3, 0\n") &&
          contains(t, "MG_FIX3, 1, 1, 0\n"));
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

    auto prescribed = parseOk("material E=210000 nu=0.3\nfix group:Bottom dof=3 value=0,0,-1.25\nforce all F=0,0,0\n");
    const auto rp = writeSolverInput(hex, prescribed, dir / "prescribed.inp");
    CHECK(rp.success);
    CHECK(contains(slurp(dir / "prescribed.inp"), "MG_FIX1, 3, 3, -1.25"));
    const auto conflicting = parseOk("material E=210000 nu=0.3\nfix all dof=1 value=0,0,0\nfix plane:x=0 dof=1 value=1,0,0\n");
    const auto conflictResult = writeSolverInput(hex, conflicting, dir / "conflict.inp");
    CHECK(!conflictResult.success && contains(conflictResult.error, "conflicting prescribed displacements") &&
          contains(conflictResult.error, "node 11") && contains(conflictResult.error, "degree of freedom 1"));
    const auto compatible = parseOk("material E=210000 nu=0.3\nfix all dof=1 value=0,0,0\nfix plane:x=0 dof=1 value=0,0,0\n");
    CHECK(writeSolverInput(hex, compatible, dir / "compatible.inp").success);

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

/**
 * @brief PrescribedDisplacementTracker: equal overlaps pass, conflicts name the first
 *        constraining support, non-finite values are rejected, and large overlapping
 *        selections stay linear in the number of nodes.
 */
void testPrescribedDisplacementTracker() {
    MeshData mesh;
    constexpr std::size_t kNodes = 200000;
    std::vector<NodeIndex> all;
    all.reserve(kNodes);
    for (std::size_t i = 0; i < kNodes; ++i)
        all.push_back(mesh.addNode(static_cast<NodeId>(i + 1), static_cast<double>(i), 0.0, 0.0));

    FixedSupport a, b, c;
    a.dof = {{true, false, false}};
    b.dof = {{true, true, false}};
    c.dof = {{true, false, false}};
    c.displacement = {{0.5, 0.0, 0.0}};

    // Equal overlapping prescriptions are accepted; only active DOFs are compared.
    PrescribedDisplacementTracker ok;
    CHECK(ok.add(0, a, all, mesh).empty());
    b.displacement = {{0.0, 7.0, 9.0}};          // DOF 3 is inactive and must be ignored
    CHECK(ok.add(1, b, all, mesh).empty());

    // A different value on an already constrained DOF names the first support and the node.
    const auto message = ok.add(2, c, {all[41]}, mesh);
    CHECK(contains(message, "conflicting prescribed displacements"));
    CHECK(contains(message, "fix #1 and fix #3"));
    CHECK(contains(message, "node 42") && contains(message, "degree of freedom 1"));

    FixedSupport bad;
    bad.displacement = {{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}};
    CHECK(contains(PrescribedDisplacementTracker{}.add(0, bad, all, mesh), "non-finite"));

    // Scaling: four fully overlapping supports over 200k nodes. The former pairwise
    // scan needed O(nodes^2 * supports) operations here (hours); the tracker is linear.
    PrescribedDisplacementTracker many;
    const auto start = std::chrono::steady_clock::now();
    FixedSupport same;
    for (std::size_t i = 0; i < 4; ++i) CHECK(many.add(i, same, all, mesh).empty());
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "PrescribedDisplacementTracker: 4 x " << kNodes << " nodes in " << seconds << " s\n";
    CHECK(seconds < 5.0);
}

} // namespace


/** Time-dependent loads and the dynamic step: parser, serializer, deck text and the refusals of the code_aster writer. */
void testTimeLoadsAndDynamic() {
    // Amplitudes and the amp= option on forces and pressures.
    {
        auto r = parseAnalysisScript("material E=210000 nu=0.3 rho=7.85e-9\n"
                                     "amplitude name=A1 t=0,0,1,1,2,0.5\n"
                                     "force group:Top F=0,0,-40 amp=A1\n"
                                     "pressure group:Right p=2 amp=A1\n"
                                     "step dynamic dt=0.01 period=1 dtmin=1e-6\n");
        CHECK(r.success);
        CHECK(r.model.amplitudes.size() == 1 && r.model.amplitudes[0].points.size() == 3);
        CHECK(r.model.amplitudes[0].points[2][0] == 2.0 && r.model.amplitudes[0].points[2][1] == 0.5);
        CHECK(r.model.forces.size() == 1 && r.model.forces[0].amplitude == "A1");
        CHECK(r.model.pressures.size() == 1 && r.model.pressures[0].amplitude == "A1");
        CHECK(r.model.jobs.front().steps.front().type == StepType::Dynamic && r.model.jobs.front().steps.front().dynamicIncrement == 0.01 && r.model.jobs.front().steps.front().dynamicPeriod == 1.0);
        CHECK(r.model.jobs.front().steps.front().dynamicMinIncrement == 1e-6 && r.model.jobs.front().steps.front().dynamicMaxIncrement == 0.0);

        // Round trip: the serialized script parses back to the same model.
        const std::string text = serializeAnalysisScript(r.model);
        CHECK(contains(text, "amplitude name=A1 t=0,0,1,1,2,0.5"));
        CHECK(contains(text, " group:Top F=0,0,-40 amp=A1"));
        CHECK(contains(text, "step dynamic dt=0.01 period=1 dtmin="));
        const auto back = parseAnalysisScript(text);
        CHECK(back.success && back.model.amplitudes.size() == 1 && back.model.forces[0].amplitude == "A1");
        CHECK(back.model.jobs.front().steps.front().type == StepType::Dynamic && back.model.jobs.front().steps.front().dynamicPeriod == 1.0);
        CHECK(std::fabs(back.model.jobs.front().steps.front().dynamicMinIncrement - 1e-6) < 1e-15);
    }

    // Refusals.
    auto fails = [](const std::string& text) { return !parseAnalysisScript(text).success; };
    const std::string base = "material E=210000 nu=0.3 rho=7.85e-9\n";
    CHECK(fails(base + "force group:Top F=0,0,-1 amp=NOPE\n"));                          // undefined amplitude
    CHECK(fails(base + "pressure group:Right p=1 amp=NOPE\n"));
    CHECK(fails(base + "amplitude name=A t=0,0,1\n"));                                  // odd count
    CHECK(fails(base + "amplitude name=A t=0,0,1,1,0.5,2\n"));                          // times not increasing
    CHECK(fails(base + "amplitude name=A t=0,0\n"));                                    // one pair only
    CHECK(fails(base + "amplitude name=A t=0,0,1,1\namplitude name=A t=0,0,2,1\n"));  // duplicate name
    CHECK(fails(base + "amplitude name=bad/name t=0,0,1,1\n"));
    CHECK(fails(base + "step dynamic period=1\n"));                                     // dt missing
    CHECK(fails(base + "step dynamic dt=2 period=1\n"));                                // dt > period
    CHECK(fails(base + "step dynamic dt=0.1 period=1 dtmin=0.5\n") == false);           // dtmin > dt is allowed

    // Deck text: amplitudes before the step, loads grouped by amplitude, dynamic step, gravity constant.
    {
        const fs::path dir = tempDir();
        MeshData hex = makeHex();
        const auto model = parseOk(base +
                                   "amplitude name=A1 t=0,0,1,1,2,0.5\n"
                                   "amplitude name=A2 t=0,1,1,0\n"
                                   "fix group:Bottom\n"
                                   "force group:Top F=0,0,-10\n"
                                   "force group:Top F=0,0,-20 amp=A1\n"
                                   "pressure group:Right p=2 amp=A2\n"
                                   "pressure group:Bottom p=3\n"
                                   "gravity g=0,0,-10\n"
                                   "step dynamic dt=0.01 period=1\n");
        const auto r = writeSolverInput(hex, model, dir / "dyn.inp");
        CHECK(r.success);
        if (!r.success) std::cerr << r.error << "\n";
        const std::string t = slurp(dir / "dyn.inp");
        CHECK(contains(t, "*AMPLITUDE, NAME=A1\n0, 0, 1, 1, 2, 0.5\n"));
        CHECK(contains(t, "*AMPLITUDE, NAME=A2\n0, 1, 1, 0\n"));
        CHECK(t.find("*AMPLITUDE") < t.find("*STEP"));                                 // model data before the step
        CHECK(contains(t, "*DYNAMIC\n0.01, 1, 1e-05, 1e+30\n"));
        CHECK(!contains(t, "*STATIC") && !contains(t, "*FREQUENCY"));
        CHECK(contains(t, "*CLOAD\nMG_LOAD1, 3, -2.5\n"));                  // -10 shared by the 4 nodes of Top
        CHECK(contains(t, "*CLOAD, AMPLITUDE=A1\nMG_LOAD2, 3, -5\n"));
        CHECK(contains(t, "*DLOAD\n1, P1, 3\nMG_EALL, GRAV, 10, 0, 0, -1\n"));  // constant pressure, then gravity
        CHECK(contains(t, "*DLOAD, AMPLITUDE=A2\n1, P4, 2\n"));
        CHECK(t.find("*CLOAD\n") < t.find("*CLOAD, AMPLITUDE=A1"));                  // constant first
        CHECK(contains(t, "NODE FILE\nU, S, E"));
        fs::remove_all(dir);
    }

    // A dynamic analysis is a free body problem check: no support, no deck.
    {
        const fs::path dir = tempDir();
        MeshData hex = makeHex();
        const auto model = parseOk(base + "step dynamic dt=0.01 period=1\n");
        CHECK(!writeSolverInput(hex, model, dir / "free.inp").success);
        fs::remove_all(dir);
    }

    // code_aster refuses what it does not write yet.
    {
        const fs::path dir = tempDir();
        MeshData hex = makeHex();
        CodeAsterOptions options;
        options.jobName = "dyn";
        const auto dynamic = parseOk(base + "fix group:Bottom\nstep dynamic dt=0.01 period=1\n");
        CHECK(!writeCodeAsterCase(hex, dynamic, dir, options).success);
        const auto timed = parseOk(base + "fix group:Bottom\namplitude name=A t=0,0,1,1\nforce group:Top F=0,0,-1 amp=A\n");
        CHECK(!writeCodeAsterCase(hex, timed, dir, options).success);
        fs::remove_all(dir);
    }
}

void testJobsStepsAndLoads() {
    const std::string common = "material E=210000 nu=0.3 rho=7.85e-9\n"
                               "fix group:Bottom\n"
                               "force id=F1 group:Top F=0,0,-10\n"
                               "pressure id=P1 group:Right p=2\n"
                               "gravity g=0,0,-10\n";
    // Parser: jobs, steps and the loads of each step; a step without loads= uses every load.
    {
        const auto model = parseOk(common +
                                   "job SETTLE\n"
                                   "step static loads=F1,gravity\n"
                                   "job LOAD\n"
                                   "step static loads=P1\n"
                                   "step static loads=F1 reset\n"
                                   "step static\n");
        CHECK(model.jobs.size() == 2);
        CHECK(model.jobs[0].id == "SETTLE" && model.jobs[0].steps.size() == 1);
        CHECK(!model.jobs[0].steps[0].allLoads && model.jobs[0].steps[0].loads.size() == 2);
        CHECK(model.jobs[1].steps.size() == 3);
        CHECK(model.jobs[1].steps[0].loads == std::vector<std::string>{"P1"} && !model.jobs[1].steps[0].resetLoads);
        CHECK(model.jobs[1].steps[1].resetLoads && model.jobs[1].steps[1].loads == std::vector<std::string>{"F1"});
        CHECK(model.jobs[1].steps[2].allLoads && !model.jobs[1].steps[2].resetLoads);
        CHECK(activeLoadIds(model, model.jobs[1].steps[2]).size() == 3);   // F1, P1 and gravity
        CHECK(activeLoadIds(model, model.jobs[0].steps[0]).size() == 2);
        CHECK(findJob(model, "") == nullptr);                              // two jobs: no implicit one
        CHECK(findJob(model, "LOAD") == &model.jobs[1]);
    }
    // Steps before any job line form the job "job"; a script without any step gets a static one.
    {
        const auto model = parseOk(common + "step static\n");
        CHECK(model.jobs.size() == 1 && model.jobs[0].id == "job" && model.jobs[0].steps.size() == 1);
        const auto none = parseAnalysisScript(common);
        CHECK(none.success && none.model.jobs.size() == 1 && none.model.jobs[0].steps.size() == 1);
    }
    // Errors: unknown load, duplicate job, job without step, bad reset value.
    {
        CHECK(!parseAnalysisScript(common + "job A\nstep static loads=NOPE\n").success);
        CHECK(hasCode(parseAnalysisScript(common + "job A\nstep static loads=NOPE\n").diagnostics, "SCRIPT_UNKNOWN_LOAD"));
        CHECK(hasCode(parseAnalysisScript(common + "job A\nstep static\njob A\nstep static\n").diagnostics, "SCRIPT_DUPLICATE"));
        CHECK(hasCode(parseAnalysisScript(common + "job EMPTY\n").diagnostics, "SCRIPT_EMPTY_JOB"));
        CHECK(hasCode(parseAnalysisScript(common + "job A\nstep static reset=maybe\n").diagnostics, "SCRIPT_BAD_RESET"));
    }
    // Round trip: the serialized script parses back to the same jobs and steps.
    {
        const auto model = parseOk(common + "job LOAD\nstep static loads=P1\nstep static loads=F1 reset\nstep static loads=none\n");
        const auto back = parseOk(serializeAnalysisScript(model));
        CHECK(back.jobs.size() == 1 && back.jobs[0].steps.size() == 3);
        CHECK(back.jobs[0].steps[1].resetLoads && back.jobs[0].steps[2].loads.empty() && !back.jobs[0].steps[2].allLoads);
    }
    // Deck: one *STEP per step; a reset zeroes the loads of the earlier steps that are not active.
    {
        const fs::path dir = tempDir();
        MeshData hex = makeHex();
        const auto cycle = parseOk(common + "job CYCLE\nstep static loads=F1\nstep static loads=P1 reset\n");
        const auto r = writeSolverInput(hex, cycle, dir / "cycle.inp", "CYCLE");
        CHECK(r.success);
        if (!r.success) std::cerr << r.error << "\n";
        const std::string t = slurp(dir / "cycle.inp");
        CHECK(t.find("*STEP") != std::string::npos && t.find("*STEP", t.find("*STEP") + 1) != std::string::npos);
        const std::size_t second = t.find("*STEP", t.find("*STEP") + 1);
        const std::string first = t.substr(0, second), last = t.substr(second);
        CHECK(contains(first, "MG_LOAD1, 3, -2.5"));                 // F1 active in step 1
        CHECK(!contains(first, "P1,") && !contains(first, "P4,"));   // no pressure yet
        CHECK(contains(last, "MG_LOAD1, 3, 0"));                     // reset: F1 set to zero in step 2
        CHECK(contains(last, "*DLOAD\n1, P4, 2"));                   // Right face (x = 1) active in step 2
        CHECK(contains(t, "*BOUNDARY"));                             // supports written once, before the steps
        CHECK(t.find("*BOUNDARY") < t.find("*STEP"));

        // Without reset the loads carry over: nothing is written in step 2 for F1.
        const auto carry = parseOk(common + "job CARRY\nstep static loads=F1\nstep static loads=P1\n");
        const auto rc = writeSolverInput(hex, carry, dir / "carry.inp", "CARRY");
        CHECK(rc.success);
        const std::string tc = slurp(dir / "carry.inp");
        const std::size_t cs = tc.find("*STEP", tc.find("*STEP") + 1);
        CHECK(cs != std::string::npos && !contains(tc.substr(cs), "MG_LOAD1, 3, 0"));

        // Several jobs: the deck needs the job name.
        const auto two = parseOk(common + "job A\nstep static loads=F1\njob B\nstep static\n");
        CHECK(!writeSolverInput(hex, two, dir / "x.inp").success);        // two jobs, none named
        CHECK(writeSolverInput(hex, cycle, dir / "y.inp").success);       // a single job needs no name
        // A job name that does not exist is refused.
        CHECK(!writeSolverInput(hex, cycle, dir / "x.inp", "NOPE").success);
        fs::remove_all(dir);
    }
    // code_aster: a job with one step is written with its active loads only; a job with several steps is refused.
    {
        const fs::path dir = tempDir();
        MeshData hex = makeHex();
        const auto model = parseOk(common + "job ONE\nstep static loads=P1\njob MANY\nstep static loads=F1\nstep static loads=P1 reset\n");
        CodeAsterOptions options;
        options.jobName = "one";
        const auto one = writeCodeAsterCase(hex, model, dir, options, "ONE");
        CHECK(one.success);
        if (one.success) {
            const std::string comm = slurp(one.commFile);
            CHECK(comm.find("PRES_REP") != std::string::npos);      // P1 active
            CHECK(comm.find("FORCE_NODALE") == std::string::npos);  // F1 is not active in this step
        }
        CHECK(!writeCodeAsterCase(hex, model, dir, options, "MANY").success);
        fs::remove_all(dir);
    }
}

/** Two volume blocks ("Housing" and "Insert") side by side, for the per-part material tests. */
MeshData makeTwoBlocks() {
    MeshData m;
    const double c[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    for (int i = 0; i < 8; ++i) m.addNode(11 + i, c[i][0], c[i][1], c[i][2]);
    for (int i = 0; i < 8; ++i) m.addNode(21 + i, c[i][0] + 1.0, c[i][1], c[i][2]);
    m.addElement("Housing", ElementType::Hexahedron8, 1, {0, 1, 2, 3, 4, 5, 6, 7});
    m.addElement("Insert", ElementType::Hexahedron8, 2, {8, 9, 10, 11, 12, 13, 14, 15});
    return m;
}

/** Per-part materials: `assign` lines, the default material for unassigned parts, and the CalculiX/code_aster writers. */
void testPartMaterials() {
    const std::string base = "material id=STEEL E=210000 nu=0.3 rho=7.85e-9 name=STEEL\n"
                             "material id=ALU E=70000 nu=0.33 rho=2.7e-9 name=ALU\n";
    {
        const auto r = parseAnalysisScript(base + "assign Housing material=ALU\nfix all\n");
        CHECK(r.success);
        CHECK(r.model.material.id == "STEEL" && r.model.materials.size() == 1 && r.model.materials[0].id == "ALU");
        CHECK(r.model.partMaterials.size() == 1 && r.model.partMaterials[0].part == "Housing");
        CHECK(materialForPart(r.model, "Housing").name == "ALU");
        CHECK(materialForPart(r.model, "Insert").name == "STEEL");   // unassigned: the default material
        CHECK(findAnalysisMaterial(r.model, "ALU") != nullptr && findAnalysisMaterial(r.model, "nope") == nullptr);

        const std::string text = serializeAnalysisScript(r.model);
        CHECK(contains(text, "material id=ALU E=70000"));
        CHECK(contains(text, "assign Housing material=ALU"));
        const auto back = parseAnalysisScript(text);
        CHECK(back.success && back.model.partMaterials.size() == 1 && back.model.materials.size() == 1);
    }
    CHECK(hasCode(parseAnalysisScript(base + "assign Housing material=BRASS\nfix all\n").diagnostics, "SCRIPT_UNKNOWN_MATERIAL"));
    CHECK(hasCode(parseAnalysisScript(base + "assign Housing\nfix all\n").diagnostics, "SCRIPT_ASSIGN"));
    CHECK(hasCode(parseAnalysisScript(base + "assign Housing material=ALU\nassign Housing material=STEEL\nfix all\n").diagnostics,
                  "SCRIPT_DUPLICATE"));
    CHECK(hasCode(parseAnalysisScript(base + "material id=ALU E=1 nu=0.3\n").diagnostics, "SCRIPT_DUPLICATE_ID"));

    {
        // Editing and removing an additional material; removing it drops the assign lines that use it.
        auto model = parseOk(base + "assign Housing material=ALU\nfix all\n");
        AnalysisMaterial alu = model.materials[0];
        alu.youngModulus = 1234.0;
        std::string err;
        CHECK(updateMaterial(model, "ALU", alu, err) && model.materials[0].youngModulus == 1234.0);
        CHECK(!updateMaterial(model, "NOPE", alu, err));
        CHECK(!eraseMaterial(model, "STEEL"));   // the default material stays
        CHECK(eraseMaterial(model, "ALU") && model.materials.empty() && model.partMaterials.empty());
    }
    const fs::path dir = tempDir();
    const MeshData two = makeTwoBlocks();
    {
        const auto model = parseOk(base + "assign Housing material=ALU\nfix plane:x=0\nforce plane:x=2 F=0,0,-1\n");
        const auto r = writeSolverInput(two, model, dir / "job.inp");
        CHECK(r.success);
        if (r.success) {
            const std::string deck = slurp(dir / "job.inp");
            CHECK(contains(deck, "*MATERIAL, NAME=STEEL") && contains(deck, "*MATERIAL, NAME=ALU"));
            CHECK(contains(deck, "*SOLID SECTION, ELSET=HOUSING, MATERIAL=ALU"));
            CHECK(contains(deck, "*SOLID SECTION, ELSET=INSERT, MATERIAL=STEEL"));
        }
        CodeAsterOptions options;
        options.jobName = "parts";
        CHECK(!writeCodeAsterCase(two, model, dir, options).success);   // one material only in code_aster
    }
    // Writer refusals: unknown part, two materials with the same CalculiX name, a missing density in a frequency analysis.
    CHECK(!writeSolverInput(two, parseOk(base + "assign Missing material=ALU\nfix all\n"), dir / "bad.inp").success);
    CHECK(!writeSolverInput(two, parseOk("material id=A E=1 nu=0.3 name=SAME\nmaterial id=B E=2 nu=0.3 name=SAME\n"
                                         "assign Housing material=B\nfix all\n"), dir / "dup.inp").success);
    CHECK(!writeSolverInput(two, parseOk("material E=210000 nu=0.3 rho=7.85e-9\nmaterial id=PLAIN E=1 nu=0.3\n"
                                         "assign Housing material=PLAIN\nfix all\nstep frequency modes=2\n"),
                            dir / "freq.inp").success);
    {
        // A volume group with a material of its own: its element leaves the block Insert and becomes the section BRACKET.
        MeshData grouped = makeTwoBlocks();
        grouped.addPhysicalGroup("Bracket", 3, 7, {2}, "");
        const auto model = parseOk(base + "assign Bracket material=ALU\nfix plane:x=0\nforce plane:x=2 F=0,0,-1\n");
        const auto r = writeSolverInput(grouped, model, dir / "group.inp");
        if (!r.success) std::cerr << "group deck: " << r.error << "\n";
        CHECK(r.success);
        if (r.success) {
            const std::string deck = slurp(dir / "group.inp");
            CHECK(contains(deck, "*SOLID SECTION, ELSET=BRACKET, MATERIAL=ALU"));
            CHECK(contains(deck, "*SOLID SECTION, ELSET=HOUSING, MATERIAL=STEEL"));   // the block keeps its default
            CHECK(!contains(deck, "ELSET=INSERT"));                                    // its only element is in the group
        }
        // A group is assigned by name only when it is a volume group: a surface group is refused.
        MeshData surface = makeTwoBlocks();
        surface.addPhysicalGroup("Skin", 2, 8, {2}, "");
        CHECK(!writeSolverInput(surface, parseOk(base + "assign Skin material=ALU\nfix all\n"), dir / "skin.inp").success);
        // An element shared by two assigned groups has no single material: refused.
        MeshData shared = makeTwoBlocks();
        shared.addPhysicalGroup("A", 3, 9, {1, 2}, "");
        shared.addPhysicalGroup("B", 3, 10, {2}, "");
        CHECK(!writeSolverInput(shared, parseOk(base + "assign A material=ALU\nassign B material=STEEL\nfix all\n"),
                                dir / "shared.inp").success);
    }
    fs::remove_all(dir);
}

int main() {
    testParser();
    testStableConditionIds();
    testPrescribedDisplacementTracker();
    testConditionCrudById();
    testSelectors();
    testWriterStatic();
    testTetFaces();
    testWriterFrequencyAndValidation();
    testTimeLoadsAndDynamic();
    testPartMaterials();
    testJobsStepsAndLoads();
    testRunner();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "analysis_setup_test: all checks passed\n";
    return 0;
}
