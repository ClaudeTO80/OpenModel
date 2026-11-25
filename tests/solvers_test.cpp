/**
 * @file solvers_test.cpp
 * @brief Solver registry (defaults, validation, per-job choice, settings keys), the `solver=` job option and the Elmer case export.
 *
 * The Elmer checks read the files OpenModel writes. ElmerSolver is not run here: the solution is NOT verified by these tests.
 */
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/ElmerSolver.hpp>
#include <OpenModel/Solvers.hpp>

#include <chrono>
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

fs::path tempDir(const std::string& tag) {
    const fs::path d = fs::temp_directory_path() /
                       ("om_solvers_test_" + tag + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(d);
    return d;
}

/** Unit hexahedron, node ids 11..18, quads on z=0, z=1 and x=1 as surface groups. */
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
    return m;
}

AnalysisModel parseOk(const std::string& text) {
    const auto r = parseAnalysisScript(text);
    CHECK(r.success);
    return r.model;
}

const char* kStaticScript =
    "material E=210000 nu=0.3 rho=7.85e-9\n"
    "fix plane:x=0\n"
    "pressure group:Top p=2.5\n"
    "job J solver=elmer\n"
    "step static\n";

void testRegistry() {
    const SolverConfig defaults = defaultSolverConfig();
    CHECK(validateSolverConfig(defaults).empty());
    CHECK(findSolver(defaults, "elmer") != nullptr);
    CHECK(findSolver(defaults, "missing") == nullptr);

    // Defaults must name a configured solver that can run the analysis type.
    SolverConfig bad = defaults;
    bad.defaults["static"] = "nothere";
    CHECK(!validateSolverConfig(bad).empty());
    bad = defaults;
    bad.defaults["frequency"] = "elmer";
    CHECK(!validateSolverConfig(bad).empty());   // Elmer runs static analyses only for now
    bad = defaults;
    bad.defaults["modal"] = "calculix";
    CHECK(!validateSolverConfig(bad).empty());   // unknown analysis type
    bad = defaults;
    bad.solvers.push_back({"calculix", SolverKind::CalculiX, "/x/ccx"});
    CHECK(!validateSolverConfig(bad).empty());   // duplicate id
    bad = defaults;
    bad.solvers.push_back({"bad id", SolverKind::Elmer, {}});
    CHECK(!validateSolverConfig(bad).empty());   // invalid id

    // Settings keys round trip, and a legacy CalculiX path becomes the solver `calculix`.
    SolverConfig custom = defaults;
    custom.solvers.push_back({"ccx-dev", SolverKind::CalculiX, "/opt/ccx-dev/ccx"});
    custom.solvers.push_back({"elmer-2", SolverKind::Elmer, "/opt/elmer/ElmerSolver"});
    custom.defaults["static"] = "elmer-2";
    std::string error;
    const SolverConfig back = configFromFields(configToFields(custom), {}, error);
    CHECK(error.empty());
    CHECK(findSolver(back, "ccx-dev") && findSolver(back, "ccx-dev")->path == "/opt/ccx-dev/ccx");
    CHECK(findSolver(back, "elmer-2") && findSolver(back, "elmer-2")->kind == SolverKind::Elmer);
    CHECK(back.defaults.at("static") == "elmer-2");
    CHECK(validateSolverConfig(back).empty());

    const SolverConfig legacy = configFromFields({}, "/usr/bin/ccx", error);
    CHECK(findSolver(legacy, "calculix") && findSolver(legacy, "calculix")->path == "/usr/bin/ccx");
    CHECK(legacy.defaults.at("static") == "calculix");

    // An entry with an unknown kind is skipped and reported.
    const SolverConfig skipped = configFromFields({{"solver.weird.kind", "fluent"}, {"solver.weird.path", "x"}}, {}, error);
    CHECK(!error.empty());
    CHECK(findSolver(skipped, "weird") == nullptr);
}

void testJobChoice() {
    const AnalysisModel m = parseOk(
        "material E=1 nu=0.3\nfix plane:x=0\nfix plane:x=1\n"
        "job A\nstep static\n"
        "job B solver=elmer\nstep static\n"
        "job C solver=nowhere\nstep static\n"
        "job D solver=code_aster\nstep frequency modes=3\n");
    CHECK(m.jobs.size() == 4);
    CHECK(m.jobs[1].solver == "elmer");
    const SolverConfig config = defaultSolverConfig();
    std::string error;
    CHECK(solverForJob(config, m.jobs[0], error) && solverForJob(config, m.jobs[0], error)->kind == SolverKind::CalculiX);
    CHECK(solverForJob(config, m.jobs[1], error) && solverForJob(config, m.jobs[1], error)->kind == SolverKind::Elmer);
    CHECK(solverForJob(config, m.jobs[2], error) == nullptr && contains(error, "nowhere"));
    CHECK(solverForJob(config, m.jobs[3], error) && solverForJob(config, m.jobs[3], error)->kind == SolverKind::CodeAster);

    // The default of a type is used when the job names none: static analyses go to Elmer here.
    SolverConfig toElmer = config;
    toElmer.defaults["static"] = "elmer";
    CHECK(solverForJob(toElmer, m.jobs[0], error) && solverForJob(toElmer, m.jobs[0], error)->kind == SolverKind::Elmer);
    CHECK(solverForJob(toElmer, m.jobs[3], error) && solverForJob(toElmer, m.jobs[3], error)->kind == SolverKind::CodeAster);

    // Elmer on a frequency job is refused.
    CHECK(solverForJob(config, AnalysisJob{"F", {AnalysisStep{StepType::Frequency}}, {}, "elmer"}, error) == nullptr);
    CHECK(contains(error, "cannot run frequency"));

    // solver= is kept by the serializer and read back.
    const AnalysisModel again = parseOk(serializeAnalysisScript(m));
    CHECK(again.jobs[1].solver == "elmer" && again.jobs[0].solver.empty());

    // A malformed solver id is a script error.
    CHECK(!parseAnalysisScript("job X solver=a/b\nstep static\n").success);
}

void testElmerStatic() {
    const MeshData mesh = makeHex();
    const AnalysisModel model = parseOk(kStaticScript);
    const fs::path dir = tempDir("elmer_ok");
    const ElmerExportResult r = writeElmerCase(mesh, model, dir, "J");
    CHECK(r.success);
    CHECK(r.error.empty());
    CHECK(r.bodies == 1);
    CHECK(r.volumeElements == 1);
    CHECK(r.nodes == 8);
    // Fix at x=0 gives four point elements; the pressure on the top gives one face element.
    CHECK(r.boundaryElements == 5);
    CHECK(fs::exists(dir / "mesh.header") && fs::exists(dir / "mesh.nodes") && fs::exists(dir / "mesh.elements") && fs::exists(dir / "mesh.boundary"));
    CHECK(fs::exists(dir / "J.sif"));

    const std::string header = slurp(dir / "mesh.header");
    CHECK(contains(header, "8 1 5"));          // nodes, bulk elements, boundary elements
    CHECK(contains(header, "808 1"));          // one hexahedron
    CHECK(contains(slurp(dir / "mesh.elements"), "1 1 808 1 2 3 4 5 6 7 8"));   // one-based node numbers
    CHECK(contains(slurp(dir / "mesh.boundary"), " 303 ") == false);
    CHECK(contains(slurp(dir / "mesh.boundary"), " 404 "));   // the top face is a quadrilateral
    CHECK(contains(slurp(dir / "mesh.boundary"), " 101 "));   // fixed nodes are point elements

    const std::string sif = slurp(dir / "J.sif");
    CHECK(contains(sif, "StressSolve"));
    CHECK(contains(sif, "Youngs Modulus = Real 210000"));
    CHECK(contains(sif, "Poisson ratio = Real 0.3"));
    CHECK(contains(sif, "Target Boundaries(1) = 1"));          // the fix
    CHECK(contains(sif, "Displacement 1 = Real 0"));
    CHECK(contains(sif, "Normal Force = Real 2.5"));           // the pressure
    CHECK(!contains(sif, "Body Force"));                        // no gravity in this script

    // Gravity adds a body force per body: rho times the acceleration.
    const AnalysisModel gravityModel = parseOk(std::string(kStaticScript) + "gravity g=0,0,-9.81\n");
    const fs::path gdir = tempDir("elmer_gravity");
    const ElmerExportResult g = writeElmerCase(mesh, gravityModel, gdir, "J");
    CHECK(g.success);
    const std::string gsif = slurp(gdir / "J.sif");
    CHECK(contains(gsif, "Body Force 1"));
    CHECK(contains(gsif, "Stress Bodyforce 3 = Real"));

    // Per-part material: the volume block named solid takes the material of its assignment.
    const AnalysisModel assigned = parseOk(
        "material E=1000 nu=0.3 rho=1 name=BASE\nmaterial id=ALU E=70000 nu=0.33 rho=2.7e-9 name=ALU\n"
        "assign solid material=ALU\nfix plane:x=0\npressure group:Top p=1\njob J\nstep static\n");
    const fs::path adir = tempDir("elmer_assign");
    CHECK(writeElmerCase(mesh, assigned, adir, "J").success);
    CHECK(contains(slurp(adir / "J.sif"), "Youngs Modulus = Real 70000"));

    // Other mesh and loads: refused with a message.
    const AnalysisModel withForce = parseOk(std::string(kStaticScript) + "force plane:x=1 F=1,0,0\n");
    CHECK(!writeElmerCase(mesh, withForce, tempDir("elmer_force"), "J").success);
    const AnalysisModel frequency = parseOk("material E=1 nu=0.3 rho=1\nfix plane:x=0\njob J\nstep frequency modes=2\n");
    CHECK(!writeElmerCase(mesh, frequency, tempDir("elmer_freq"), "J").success);
    const AnalysisModel noSupport = parseOk("material E=1 nu=0.3\npressure group:Top p=1\njob J\nstep static\n");
    CHECK(!writeElmerCase(mesh, noSupport, tempDir("elmer_nosupport"), "J").success);
    const AnalysisModel noDensity = parseOk("material E=1 nu=0.3\nfix plane:x=0\ngravity g=0,0,-9.81\njob J\nstep static\n");
    CHECK(!writeElmerCase(mesh, noDensity, tempDir("elmer_nodensity"), "J").success);
    const AnalysisModel badPart = parseOk("material id=ALU E=1 nu=0.3\nassign Nowhere material=ALU\nfix plane:x=0\njob J\nstep static\n");
    CHECK(!writeElmerCase(mesh, badPart, tempDir("elmer_badpart"), "J").success);
    const AnalysisModel twoSteps = parseOk("material E=1 nu=0.3\nfix plane:x=0\njob J\nstep static\nstep static reset\n");
    CHECK(!writeElmerCase(mesh, twoSteps, tempDir("elmer_steps"), "J").success);
}

void testElmerLaunch() {
    CHECK(findElmerExecutable("/opt/elmer/ElmerSolver") == "/opt/elmer/ElmerSolver");
    const auto command = elmerLaunchCommand("/opt/elmer/ElmerSolver", fs::path("/tmp/case/J.sif"));
    CHECK(command.size() == 2 && command[1] == "J.sif");
}

} // namespace

int main() {
    testRegistry();
    testJobChoice();
    testElmerStatic();
    testElmerLaunch();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "solvers_test: all checks passed\n";
    return 0;
}
