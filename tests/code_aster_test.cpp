/**
 * @file code_aster_test.cpp
 * @brief Tests of the code_aster export, the ASTER mesh reader and the result table reader.
 *
 * These tests check the files OpenModel writes and reads. They do not run code_aster: that the generated command file is
 * accepted by a real installation is NOT verified here.
 */
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/CodeAster.hpp>
#include <OpenModel/ResultBinding.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

fs::path tempDir() {
    const fs::path d = fs::temp_directory_path() / ("om_aster_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(d);
    return d;
}

/** Unit hexahedron, node ids 11..18, quads on z=0, z=1 and x=1 as surface groups, a node set. */
MeshData makeHex() {
    MeshData m;
    const double c[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    for (int i = 0; i < 8; ++i) m.addNode(11 + i, c[i][0], c[i][1], c[i][2]);
    m.addElement("solid", ElementType::Hexahedron8, 1, {0, 1, 2, 3, 4, 5, 6, 7});
    m.addElement("faces", ElementType::Quadrilateral4, 101, {0, 3, 2, 1});
    m.addElement("faces", ElementType::Quadrilateral4, 102, {4, 5, 6, 7});
    m.addElement("faces", ElementType::Quadrilateral4, 103, {1, 2, 6, 5});
    m.addPhysicalGroup("Bottom", 2, 1, {101});
    m.addPhysicalGroup("Top", 2, 2, {102});
    m.addPhysicalGroup("Right", 2, 3, {103});
    m.addPhysicalGroup("Body", 3, 4, {1});
    m.addNodeSet("tip nodes", {1, 2});
    return m;
}

AnalysisModel parseOk(const std::string& text) {
    const auto r = parseAnalysisScript(text);
    CHECK(r.success);
    return r.model;
}

bool sameMesh(const MeshData& a, const MeshData& b) {
    if (a.getNumNodes() != b.getNumNodes() || a.getNumElements() != b.getNumElements()) return false;
    for (std::size_t i = 0; i < a.getNumNodes(); ++i) {
        if (a.nodeId(i) != b.nodeId(i)) return false;
        const auto p = a.nodeCoordinates(i), q = b.nodeCoordinates(i);
        for (int k = 0; k < 3; ++k) if (p[k] != q[k]) return false;   // 17 digits: exact round trip
    }
    for (const auto& block : a.getElementBlocks()) {
        bool found = false;
        for (const auto& other : b.getElementBlocks()) {
            if (other.type != block.type || other.ids != block.ids) continue;
            found = true;
            for (std::size_t i = 0; i < block.connectivity.size(); ++i)
                if (a.nodeId(block.connectivity[i]) != b.nodeId(other.connectivity[i])) return false;
        }
        if (!found) return false;
    }
    return true;
}

void testStaticExport() {
    const auto mesh = makeHex();
    const auto model = parseOk(
        "material E=210000 nu=0.3 rho=7.85e-9\n"
        "fix group:Bottom\n"
        "fix plane:x=0 dof=1\n"
        "force group:Right F=0,-100,0\n"
        "pressure group:Top p=2.5\n"
        "gravity g=0,0,-9810\n");
    const auto dir = tempDir();
    CodeAsterOptions options;
    options.jobName = "beam";
    options.solver = "MUMPS";
    options.ncpus = 2;
    options.version = "stable";
    const auto r = writeCodeAsterCase(mesh, model, dir, options);
    CHECK(r.success);
    if (!r.success) { std::cerr << r.error << "\n"; return; }
    CHECK(r.nodes == 8 && r.elements == 4 && r.volumeElements == 1);
    CHECK(r.fixedNodes == 6 && r.loadedNodes == 4 && r.pressureFaces == 1);
    CHECK(r.meshFile.filename() == "beam.mail" && r.commFile.filename() == "beam.comm" && r.exportFile.filename() == "beam.export");

    const std::string mail = slurp(r.meshFile), comm = slurp(r.commFile), exp = slurp(r.exportFile);
    CHECK(contains(mail, "COOR_3D\nN11 0 0 0\n"));
    CHECK(contains(mail, "HEXA8\nM1 N11 N12 N13 N14 N15 N16 N17 N18\nFINSF"));
    CHECK(contains(mail, "QUAD4\nM101 N11 N14 N13 N12\n"));
    CHECK(contains(mail, "GROUP_MA NOM = OM_VOLUME\n M1\nFINSF"));
    CHECK(contains(mail, "GROUP_MA NOM = TOP\n M102\nFINSF"));
    CHECK(contains(mail, "GROUP_NO NOM = TIP_NODES\n N12 N13\nFINSF"));       // 'tip nodes' sanitised
    CHECK(contains(mail, "GROUP_NO NOM = OM_FIX1\n"));
    CHECK(contains(mail, "GROUP_NO NOM = OM_LOAD1\n N12 N13 N17 N16\nFINSF") || contains(mail, "GROUP_NO NOM = OM_LOAD1\n"));
    CHECK(mail.size() > 4 && mail.substr(mail.size() - 4) == "FIN\n");

    CHECK(contains(comm, "DEBUT(LANG='EN')"));
    CHECK(contains(comm, "LIRE_MAILLAGE(FORMAT='ASTER', UNITE=20)"));
    CHECK(contains(comm, "MODELISATION='3D'"));
    CHECK(contains(comm, "E=210000.0, NU=0.3, RHO=7.85e-09"));
    CHECK(contains(comm, "_F(GROUP_NO='OM_FIX1', DX=0.0, DY=0.0, DZ=0.0)"));
    CHECK(contains(comm, "_F(GROUP_NO='OM_FIX2', DX=0.0)"));
    CHECK(contains(comm, "FY=-25.0"));                                          // 100 N shared by 4 nodes
    CHECK(!contains(comm, "FX="));
    CHECK(contains(comm, "PRES_REP=(") && contains(comm, "_F(GROUP_MA='TOP', PRES=2.5)"));
    CHECK(contains(comm, "PESANTEUR=_F(GRAVITE=9810.0, DIRECTION=(0.0, 0.0, -1.0)"));
    CHECK(contains(comm, "SOLVEUR=_F(METHODE='MUMPS')"));
    CHECK(contains(comm, "MECA_STATIQUE(") && contains(comm, "NOM_CHAM='SIGM_NOEU'") && contains(comm, "NOM_CHAM='EPSI_NOEU'"));
    CHECK(contains(comm, "UNITE=80") && contains(comm, "UNITE=81") && contains(comm, "UNITE=82"));
    CHECK(comm.size() > 6 && comm.substr(comm.size() - 6) == "FIN()\n");

    CHECK(contains(exp, "P ncpus 2") && contains(exp, "P version stable") && contains(exp, "P time_limit 3600"));
    CHECK(contains(exp, "F comm " + r.commFile.string() + " D 1"));
    CHECK(contains(exp, "F mail " + r.meshFile.string() + " D 20"));
    CHECK(contains(exp, "beam_disp.tab R 80") && contains(exp, "beam_stress.tab R 81") && contains(exp, "beam_strain.tab R 82"));
    fs::remove_all(dir);
}

void testFrequencyExport() {
    const auto mesh = makeHex();
    const auto dir = tempDir();
    auto r = writeCodeAsterCase(mesh, parseOk("material E=1000 nu=0.25 rho=1\nfix group:Bottom\nstep frequency modes=3\n"), dir);
    CHECK(r.success);
    const std::string comm = slurp(r.commFile), exp = slurp(r.exportFile);
    CHECK(contains(comm, "CALC_MODES(") && contains(comm, "NMAX_FREQ=3") && contains(comm, "MASS_MECA") && contains(comm, "CHARGE=support"));
    CHECK(contains(comm, "fort.81") && contains(comm, "NOM_CHAM='DEPL'") && !contains(comm, "MECA_STATIQUE"));
    CHECK(contains(exp, "job_modes_disp.tab R 80") && contains(exp, "job_modes.tab R 81"));
    // Without supports there is no boundary condition object at all.
    r = writeCodeAsterCase(mesh, parseOk("material E=1000 nu=0.25 rho=1\nstep frequency modes=2\n"), dir);
    CHECK(r.success);
    const std::string free = slurp(r.commFile);
    CHECK(!contains(free, "AFFE_CHAR_MECA") && !contains(free, "CHARGE=") && contains(free, "CALC_MODES("));
    fs::remove_all(dir);
}

void testExportErrors() {
    const auto mesh = makeHex();
    const auto dir = tempDir();
    auto fails = [&](const std::string& script, const std::string& message, CodeAsterOptions options = {}) {
        const auto parsed = parseAnalysisScript(script);
        if (!parsed.success) { CHECK(false); return; }
        const auto r = writeCodeAsterCase(mesh, parsed.model, dir, options);
        CHECK(!r.success);
        CHECK(contains(r.error, message));
        if (!contains(r.error, message)) std::cerr << "  got: " << r.error << "\n";
    };
    fails("material E=1 nu=0.3\nforce all F=1,0,0\n", "needs at least one support");
    fails("material E=1 nu=0.3\nfix group:Nope\n", "Unknown group");
    fails("material E=1 nu=0.3\nfix all\npressure group:Body p=1\n", "not a surface group");
    fails("material E=1 nu=0.3\nfix all\npressure group:Missing p=1\n", "unknown surface group");
    fails("material E=1 nu=0.3\nfix all\ngravity g=0,0,-1\n", "density");
    CodeAsterOptions bad;
    bad.jobName = "../evil";
    fails("material E=1 nu=0.3\nfix all\n", "Job name", bad);
    bad = {};
    bad.solver = "FOO";
    fails("material E=1 nu=0.3\nfix all\n", "Unknown linear solver", bad);

    // A surface element between two volume elements cannot carry a pressure.
    MeshData two;
    for (int i = 0; i < 5; ++i) two.addNode(i + 1, i == 1 ? 1 : 0, i == 2 ? 1 : 0, i == 3 ? 1 : 0);   // 1:(0,0,0) 2:(1,0,0) 3:(0,1,0) 4:(0,0,1) 5:(0,0,0)
    two.addNode(6, 1, 1, 1);
    two.addElement("solid", ElementType::Tetrahedron4, 1, {0, 1, 2, 3});
    two.addElement("solid", ElementType::Tetrahedron4, 2, {1, 2, 3, 5});   // shares face 2-3-4
    two.addElement("faces", ElementType::Triangle3, 10, {1, 2, 3});
    two.addPhysicalGroup("Inner", 2, 1, {10});
    const auto parsed = parseAnalysisScript("material E=1 nu=0.3\nfix all\npressure group:Inner p=1\n");
    const auto r = writeCodeAsterCase(two, parsed.model, dir);
    CHECK(!r.success && contains(r.error, "between two volume elements"));

    // Ids with more than 7 digits.
    MeshData big = makeHex();
    big.addNode(12345678, 5, 5, 5);
    const auto p2 = parseAnalysisScript("material E=1 nu=0.3\nfix plane:x=0\n");
    const auto rb = writeCodeAsterCase(big, p2.model, dir);
    CHECK(!rb.success && contains(rb.error, "12345678"));

    // Surface mesh only.
    MeshData surface;
    surface.addNode(1, 0, 0, 0); surface.addNode(2, 1, 0, 0); surface.addNode(3, 0, 1, 0);
    surface.addElement("s", ElementType::Triangle3, 1, {0, 1, 2});
    const auto rs = writeCodeAsterCase(surface, p2.model, dir);
    CHECK(!rs.success && contains(rs.error, "no volume elements"));
    fs::remove_all(dir);
}

void testGenericSurfacePressure() {
    MeshData mesh = makeHex();
    mesh.addMeshGroup({"CAD_TOP", 2, MeshGroupSource::CAD, {44}, {102}, {4, 5, 6, 7}});
    const auto model = parseOk("material E=210000 nu=0.3\nfix all\npressure group:CAD_TOP p=1.5\n");
    const auto dir = tempDir();
    const auto r = writeCodeAsterCase(mesh, model, dir, {});
    CHECK(r.success);
    if (r.success) {
        const auto mail = slurp(r.meshFile);
        const auto comm = slurp(r.commFile);
        CHECK(contains(mail, "GROUP_MA NOM = CAD_TOP\n M102\nFINSF"));
        CHECK(contains(comm, "_F(GROUP_MA='CAD_TOP', PRES=1.5)"));
        CHECK(r.pressureFaces == 1);
    } else {
        std::cerr << r.error << "\n";
    }
    fs::remove_all(dir);
}

void testGenericMeshGroupExport() {
    MeshData mesh;
    const auto n1 = mesh.addNode(1, 0, 0, 0);
    const auto n2 = mesh.addNode(2, 1, 0, 0);
    const auto n3 = mesh.addNode(3, 1, 1, 0);
    mesh.addElement("cad_edges", ElementType::Line2, 101, {n1, n2});
    mesh.addElement("cad_vertices", ElementType::Point1, 102, {n3});
    mesh.addMeshGroup({"CAD_EDGE", 1, MeshGroupSource::CAD, {4}, {101}, {n1, n2}});
    mesh.addMeshGroup({"CAD_VERTEX", 0, MeshGroupSource::CAD, {8}, {}, {n3}});

    const auto dir = tempDir();
    const auto error = writeCodeAsterMesh(dir / "groups.mail", mesh);
    CHECK(error.empty());
    if (error.empty()) {
        const auto mail = slurp(dir / "groups.mail");
        CHECK(contains(mail, "GROUP_MA NOM = CAD_EDGE\n M101\nFINSF"));
        CHECK(contains(mail, "GROUP_NO NOM = CAD_EDGE_NODES\n N1 N2\nFINSF"));
        CHECK(contains(mail, "GROUP_NO NOM = CAD_VERTEX_NODES\n N3\nFINSF"));
    }
    fs::remove_all(dir);
}

void testMeshRoundTrip() {
    const auto dir = tempDir();
    // Hexahedron mesh with groups.
    const MeshData hex = makeHex();
    CHECK(writeCodeAsterMesh(dir / "hex.mail", hex).empty());
    MeshData back;
    CodeAsterMeshImporter importer;
    CHECK(importer.canRead("x.MAIL") && !importer.canRead("x.med"));
    const auto imported = importer.read(dir / "hex.mail", back);
    CHECK(imported);
    CHECK(imported.nodesRead == 8 && imported.elementsRead == 4 && imported.nodeSetsRead == 1);
    CHECK(sameMesh(hex, back));
    bool top = false, body = false;
    for (const auto& g : back.getPhysicalGroups()) {
        if (g.name == "TOP") top = g.dimension == 2 && g.elementIds == std::vector<ElementId>{102};
        if (g.name == "BODY") body = g.dimension == 3 && g.elementIds == std::vector<ElementId>{1};
    }
    CHECK(top && body);
    CHECK(back.getNodeSets().count("TIP_NODES") == 1);

    // Every supported type, with distinct node numbers so that a wrong permutation is noticed.
    MeshData all;
    for (int i = 0; i < 40; ++i) all.addNode(100 + i, i * 0.5, std::sin(i), std::cos(i * 0.3));
    auto conn = [](int first, int n) { std::vector<NodeIndex> c; for (int i = 0; i < n; ++i) c.push_back(static_cast<NodeIndex>(first + i)); return c; };
    struct Entry { ElementType type; int count; } entries[] = {
        {ElementType::Point1, 1}, {ElementType::Line2, 2}, {ElementType::Line3, 3}, {ElementType::Triangle3, 3}, {ElementType::Triangle6, 6},
        {ElementType::Quadrilateral4, 4}, {ElementType::Quadrilateral8, 8}, {ElementType::Quadrilateral9, 9}, {ElementType::Tetrahedron4, 4},
        {ElementType::Tetrahedron10, 10}, {ElementType::Hexahedron8, 8}, {ElementType::Hexahedron20, 20}, {ElementType::Prism6, 6},
        {ElementType::Prism15, 15}, {ElementType::Pyramid5, 5}};
    ElementId id = 1;
    for (const auto& e : entries) { all.addElement("b" + std::to_string(id), e.type, id, conn(3, e.count)); ++id; }
    CHECK(writeCodeAsterMesh(dir / "all.mail", all).empty());
    MeshData allBack;
    const auto r2 = importer.read(dir / "all.mail", allBack);
    CHECK(r2);
    CHECK(sameMesh(all, allBack));
    // The Prism15 permutation really changes the written order.
    std::string text = slurp(dir / "all.mail");
    for (std::size_t at; (at = text.find("\n ")) != std::string::npos;) text.replace(at, 2, " ");   // join continuation lines
    CHECK(contains(text, "PENTA15\nM14 N103 N104 N105 N106 N107 N108 N109 N110 N111 N115 N116 N117 N112 N113 N114\n"));

    // Hexahedron27 is refused.
    MeshData h27;
    for (int i = 0; i < 27; ++i) h27.addNode(i + 1, i, 0, 0);
    h27.addElement("h", ElementType::Hexahedron27, 1, conn(0, 27));
    CHECK(contains(writeCodeAsterMesh(dir / "h27.mail", h27), "does not support"));

    // Hand written file: lower case keywords, comment, continuation lines, odd names (renumbered), unknown block.
    {
        std::ofstream f(dir / "hand.mail");
        f << "% a comment\ntitre\nHand written\nfinsf\ncoor_3d\nA 0 0 0\nB 1.0D0 0 0\nC 0 1 0\nD 0 0 1\nfinsf\n"
             "tetra4\nE1 A B\n C D\nfinsf\nstrange\nfoo\nfinsf\nGROUP_NO NOM=ALL\nA B C D\nFINSF\nGROUP_MA NOM = G\n E1\nFINSF\nFIN\n";
    }
    MeshData hand;
    const auto r3 = importer.read(dir / "hand.mail", hand);
    CHECK(r3);
    CHECK(hand.getNumNodes() == 4 && hand.getNumElements() == 1);
    CHECK(hand.nodeId(1) == 2 && hand.nodeCoordinates(1)[0] == 1.0);   // 1.0D0 read; ids 1..n after the warning
    bool renumber = false, unknown = false;
    for (const auto& d : r3.diagnostics) { renumber = renumber || d.code == "ASTER_IDS_RENUMBERED"; unknown = unknown || d.code == "ASTER_UNKNOWN_BLOCK"; }
    CHECK(renumber && unknown);
    CHECK(hand.getNodeSets().count("ALL") == 1 && hand.getPhysicalGroups().size() == 1);

    // Errors.
    {
        std::ofstream f(dir / "bad1.mail");
        f << "COOR_3D\nN1 0 0 0\nFINSF\nTETRA4\nM1 N1 N2 N3 N4\nFINSF\nFIN\n";
    }
    MeshData bad;
    const auto r4 = importer.read(dir / "bad1.mail", bad);
    CHECK(!r4);
    {
        std::ofstream f(dir / "bad2.mail");
        f << "COOR_3D\nN1 0 x 0\nFINSF\nFIN\n";
    }
    CHECK(!importer.read(dir / "bad2.mail", bad));
    {
        std::ofstream f(dir / "med.mail", std::ios::binary);
        f << "\x89HDF\r\n\x1a\n";
    }
    const auto r5 = importer.read(dir / "med.mail", bad);
    CHECK(!r5 && !r5.diagnostics.empty() && contains(r5.diagnostics.back().message, "MED"));
    CHECK(!importer.read(dir / "missing.mail", bad));
    fs::remove_all(dir);
}

FemResults readTable(const std::string& text, bool modal, ResultImportResult* outcome = nullptr) {
    CodeAsterTableImporter importer;
    FemResults results;
    std::istringstream in(text);
    const auto r = importer.parse(in, results, modal);
    if (outcome) *outcome = r;
    return results;
}

const ResultField* findField(const FemResults& r, const std::string& name, std::size_t step = 0) {
    for (const auto& f : r.fields) if (f.name == name && f.step == step) return &f;
    return nullptr;
}

void testTables() {
    // Static displacements: '#' header, comma separated, node names.
    {
        ResultImportResult out;
        const auto r = readTable("#NOEUD,DX,DY,DZ\nN1,0.0,-1.0E-03,0.0\nN2,1.5E-04,-2.0E-03,0.0\n", false, &out);
        CHECK(out);
        CHECK(r.steps.size() == 1 && r.steps[0].kind == StepKind::Static);
        const auto* d = findField(r, "DISP");
        CHECK(d && d->ids == std::vector<std::uint64_t>({1, 2}) && d->components.size() == 3);
        CHECK(d && d->values[1] == -1.0e-3 && d->values[3] == 1.5e-4);
    }
    // Stress columns come in code_aster order (SIXY SIXZ SIYZ) and are reordered to SXX SYY SZZ SXY SYZ SZX; blanks as separator, extra title.
    {
        const auto r = readTable("# some title line\n#   NOEUD   SIXX   SIYY   SIZZ   SIXY   SIXZ   SIYZ\n   N7  1 2 3 4 5 6\n   N8  7 8 9 10 11 12\n", false);
        const auto* s = findField(r, "STRESS");
        CHECK(s && s->ids.size() == 2 && s->components[3] == "SXY" && s->components[4] == "SYZ" && s->components[5] == "SZX");
        CHECK(s && s->values[3] == 4 && s->values[4] == 6 && s->values[5] == 5);   // SYZ = SIYZ (6), SZX = SIXZ (5)
    }
    // Several orders with INST become time steps; the order column is dropped from the fields; unknown columns are scalars.
    {
        const auto r = readTable("NUME_ORDRE;INST;NOEUD;DX;DY;DZ;TEMP\n1;0.5;N1;1;2;3;20\n1;0.5;N2;1;2;3;21\n2;1.0;N1;4;5;6;22\n2;1.0;N2;4;5;6;23\n", false);
        CHECK(r.steps.size() == 2 && r.steps[1].kind == StepKind::Time && r.steps[1].value == 1.0);
        CHECK(findField(r, "DISP", 1) && findField(r, "TEMP", 0) && findField(r, "TEMP", 1)->values[1] == 23);
    }
    // Eigenvalue table and mode shapes, merged in the order of codeAsterResultFiles().
    {
        ResultImportResult out;
        FemResults base = readTable("#NUME_ORDRE,FREQ\n1,10.5\n2,21.0\n", true, &out);
        CHECK(out && base.eigenmodes.size() == 2 && std::fabs(base.eigenmodes[1].omega - 2 * 3.14159265358979 * 21.0) < 1e-6);
        CHECK(base.steps.size() == 2 && base.steps[0].kind == StepKind::Frequency && base.steps[0].value == 10.5 && base.steps[1].mode == 2);
        const auto shapes = readTable("#NUME_ORDRE,NOEUD,DX,DY,DZ\n1,N1,1,0,0\n1,N2,2,0,0\n2,N1,0,1,0\n2,N2,0,2,0\n", true);
        CHECK(shapes.steps.size() == 2 && shapes.steps[1].kind == StepKind::Frequency);
        const auto merged = mergeResults(base, shapes);
        CHECK(merged);
        CHECK(base.steps.size() == 2 && base.modeSteps().size() == 2 && base.steps[0].value == 10.5);
        CHECK(findField(base, "DISP", 0) && findField(base, "DISP", 1)->values[4] == 2);
    }
    // Malformed tables.
    {
        ResultImportResult out;
        readTable("", false, &out);
        CHECK(!out && out.diagnostics.front().code == "ASTER_TAB_NO_HEADER");
        readTable("NOEUD,DX\nN1,abc\n", false, &out);
        CHECK(!out && out.diagnostics.back().code == "ASTER_TAB_BAD_NUMBER");
        readTable("NOEUD,DX,DY\nN1,1\n", false, &out);
        CHECK(!out && out.diagnostics.back().code == "ASTER_TAB_SHORT_ROW");
        readTable("NOEUD,DX\n", false, &out);
        CHECK(!out && out.diagnostics.back().code == "ASTER_TAB_EMPTY");
        readTable("NOEUD,DX\nFOO,1\nN3,2\n", false, &out);
        CHECK(out);   // one row with a nameless node skipped, with a warning
        bool warned = false;
        for (const auto& d : out.diagnostics) warned = warned || d.code == "ASTER_TAB_BAD_NODE";
        CHECK(warned);
        readTable("NUME_ORDRE,DX\n1,2\n", false, &out);
        CHECK(!out);
    }
    // File dispatch by extension and name.
    {
        const auto dir = tempDir();
        { std::ofstream f(dir / "job_modes.tab"); f << "NUME_ORDRE,FREQ\n1,5.0\n"; }
        { std::ofstream f(dir / "job_modes_disp.tab"); f << "NUME_ORDRE,NOEUD,DX,DY,DZ\n1,N1,0,0,1\n"; }
        { std::ofstream f(dir / "job_disp.tab"); f << "NOEUD,DX,DY,DZ\nN1,0,0,1\n"; }
        FemResults r;
        CHECK(readResults(dir / "job_modes_disp.tab", r) && r.steps[0].kind == StepKind::Frequency);
        CHECK(readResults(dir / "job_disp.tab", r) && r.steps[0].kind == StepKind::Static);
        const auto files = codeAsterResultFiles(dir, "job");
        CHECK(files.size() == 3 && files[0].filename() == "job_modes.tab");
        FemResults merged;
        CHECK(readResultsMerged({dir / "job_modes.tab", dir / "job_modes_disp.tab"}, merged));
        CHECK(merged.eigenmodes.size() == 1 && merged.fields.size() == 1);
        removeCodeAsterOutputs(dir, "job");
        CHECK(codeAsterResultFiles(dir, "job").empty());
        fs::remove_all(dir);
    }
}

void testLauncher() {
    CHECK(findCodeAsterExecutable("/opt/aster/bin/run_aster") == "/opt/aster/bin/run_aster");
    const auto cmd = codeAsterLaunchCommand("run_aster", "/a/b/job.export");
    CHECK(cmd.size() == 2 && cmd[0] == "run_aster" && cmd[1] == "job.export");
}

/** Whole chain on the hexahedron: export, fake results, bind to the mesh. */
void testBindToMesh() {
    const auto mesh = makeHex();
    const auto dir = tempDir();
    { std::ofstream f(dir / "job_disp.tab"); f << "#NOEUD,DX,DY,DZ\n"; for (int i = 0; i < 8; ++i) f << "N" << 11 + i << ",0,0," << -0.001 * i << "\n"; }
    { std::ofstream f(dir / "job_stress.tab"); f << "#NOEUD,SIXX,SIYY,SIZZ,SIXY,SIXZ,SIYZ\n"; for (int i = 0; i < 8; ++i) f << "N" << 11 + i << ",10,0,0,0,0,0\n"; }
    FemResults results;
    const auto read = readResultsMerged(codeAsterResultFiles(dir, "job"), results);
    CHECK(read);
    WebViewOptions view;
    BindOptions bind;
    const auto bound = bindResults(mesh, results, bind, view);
    CHECK(bound.success && bound.hasDisplacement);
    CHECK(bound.fieldsBound > 6);   // components plus derived von Mises, principals, magnitude...
    // Uniaxial stress of 10: von Mises 10 at every node (name as produced by the binder, searched by substring).
    bool vonMises = false;
    for (const auto& f : view.fields)
        if (f.name.find("von") != std::string::npos || f.name.find("Mises") != std::string::npos || f.name.find("mises") != std::string::npos) {
            vonMises = true;
            for (const double v : f.values) CHECK(std::fabs(v - 10.0) < 1e-9);
        }
    CHECK(vonMises);
    fs::remove_all(dir);
}

} // namespace

int main() {
    testStaticExport();
    testFrequencyExport();
    testGenericMeshGroupExport();
    testGenericSurfacePressure();
    testExportErrors();
    testMeshRoundTrip();
    testTables();
    testLauncher();
    testBindToMesh();
    if (g_failures) { std::cerr << g_failures << " check(s) failed\n"; return 1; }
    std::cout << "code_aster tests passed\n";
    return 0;
}
