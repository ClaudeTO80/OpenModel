/**
 * @file heat2d_mesh_script_test.cpp
 * @brief Parsing of the mesh heat script (`analysis heat 2d mesh`): valid scripts, the values they produce, the error
 *        messages with their line numbers, and the CSV output. The mesh itself is not built here (see the Gmsh tests).
 */
#include <OpenModel/Heat2DMeshScript.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace OpenModel::Heat2D;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool hasError(const std::vector<std::string>& errors, const std::string& needle) {
    for (const auto& e : errors)
        if (e.find(needle) != std::string::npos) return true;
    return false;
}

/** @brief Parses a script that must be rejected; returns true when the expected message is among the errors. */
bool rejects(const std::string& text, const std::string& needle) {
    MeshScriptProblem p;
    std::vector<std::string> errors;
    if (parseMeshScript(text, p, errors)) return false;
    if (!hasError(errors, needle)) {
        std::cerr << "  expected '" << needle << "', got:\n";
        for (const auto& e : errors) std::cerr << "    " << e << "\n";
        return false;
    }
    return true;
}

const std::string kValidHead =
    "analysis heat 2d mesh\n"
    "geometry step file=plate.step size=0.05\n"
    "material k=1 rhocp=2 thickness=0.5\n";

void testValidTransient() {
    const std::string text = kValidHead +
        "boundary tag=1 temperature T=0\n"
        "boundary tag=2 flux q=3\n"
        "boundary tag=3 convection h=25 Tinf=20 emissivity=0.8\n"
        "heat q=7\n"
        "initial T=20\n"
        "job MAIN solver=internal\n"
        "step transient dt=0.01 end=0.1 theta=0.5 capacity=lumped\n";
    MeshScriptProblem p;
    std::vector<std::string> errors;
    CHECK(parseMeshScript(text, p, errors));
    CHECK(errors.empty());
    CHECK(p.stepFile == "plate.step");
    CHECK(p.size == 0.05);
    CHECK(p.model.thickness == 0.5);
    CHECK(p.model.boundaries.size() == 3);
    CHECK(p.model.boundaries.at(1).kind == BoundaryKind::Temperature);
    CHECK(p.model.boundaries.at(1).value(0.0) == 0.0);
    CHECK(p.model.boundaries.at(2).kind == BoundaryKind::HeatFlux);
    CHECK(p.model.boundaries.at(2).value(0.0) == 3.0);
    CHECK(p.model.boundaries.at(3).kind == BoundaryKind::Convection);
    CHECK(p.model.boundaries.at(3).h == 25.0);
    CHECK(p.model.boundaries.at(3).emissivity == 0.8);
    CHECK(p.model.boundaries.at(3).ambient(0.0) == 20.0);
    CHECK(p.model.conductivity(0.0, 0.0, 0.0) == 1.0);
    CHECK(p.model.volumetricHeatCapacity(0.0, 0.0) == 2.0);
    CHECK(p.model.source(0.0, 0.0, 0.0) == 7.0);
    CHECK(p.hasInitial && p.initialT == 20.0);
    CHECK(!p.options.steady);
    CHECK(p.options.timeStep == 0.01);
    CHECK(p.options.endTime == 0.1);
    CHECK(p.options.theta == 0.5);
    CHECK(p.options.capacity == OpenModel::Heat1D::Capacity::Lumped);
}

void testValidSteady() {
    const std::string text = kValidHead +
        "boundary tag=1 temperature T=0\n"
        "step steady\n";
    MeshScriptProblem p;
    std::vector<std::string> errors;
    CHECK(parseMeshScript(text, p, errors));
    CHECK(errors.empty());
    CHECK(p.options.steady);
    CHECK(!p.hasInitial);
    CHECK(p.model.thickness == 0.5);
    CHECK(!p.model.source);
}

void testDefaults() {
    // Thickness defaults to 1, theta and capacity keep the Options defaults; a failing parse leaves the output unchanged.
    const std::string text =
        "analysis heat 2d mesh\n"
        "geometry step file=a.step size=0.1\n"
        "material k=1 rhocp=1\n"
        "step transient dt=0.1 end=1\n";
    MeshScriptProblem p;
    std::vector<std::string> errors;
    CHECK(parseMeshScript(text, p, errors));
    CHECK(p.model.thickness == 1.0);
    CHECK(p.options.theta == OpenModel::Heat1D::Options().theta);
    CHECK(p.options.capacity == OpenModel::Heat1D::Options().capacity);
}

void testErrorsWithLineNumbers() {
    CHECK(rejects(kValidHead + "foo bar\nstep steady\n", "line 4: unknown command 'foo'"));
    CHECK(rejects(kValidHead + "boundary tag=1 temperature T=0\nboundary tag=1 temperature T=5\nstep steady\n",
                  "line 5: tag 1 already has a condition"));
    CHECK(rejects(kValidHead + "boundary tag=1 radiation T=0\nstep steady\n",
                  "line 4: boundary kind must be temperature, flux or convection"));
    CHECK(rejects(kValidHead + "boundary tag=1.5 temperature T=0\nstep steady\n",
                  "line 4: tag must be a whole number of at least 1"));
    CHECK(rejects(kValidHead + "boundary tag=0 temperature T=0\nstep steady\n",
                  "tag must be a whole number of at least 1"));
    CHECK(rejects(kValidHead + "boundary tag=1 temperature\nstep steady\n", "line 4: missing T="));
    CHECK(rejects(kValidHead + "step transient dt=0.1 end=1 capacity=mass\n", "capacity must be consistent or lumped"));
    CHECK(rejects(kValidHead + "step transient dt=0.1 end=1 theta=abc\n", "theta= is not a number"));
    CHECK(rejects(kValidHead + "step sometimes\n", "step must be steady or transient"));
    CHECK(rejects(kValidHead + "step\n", "step needs steady or transient"));
    CHECK(rejects(kValidHead + "step steady\nstep steady\n", "line 5: step repeated"));
    CHECK(rejects(kValidHead + "job A solver=calculix\nstep steady\n", "only the internal solver runs mesh models"));
    CHECK(rejects("analysis heat 2d\ngeometry step file=a.step size=0.1\nmaterial k=1 rhocp=1\nstep steady\n",
                  "line 1: expected 'analysis heat 2d mesh'"));
    CHECK(rejects("analysis heat 2d mesh\ngeometry step file=a.step size=-0.1\nmaterial k=1 rhocp=1\nstep steady\n",
                  "size must be positive"));
    CHECK(rejects("analysis heat 2d mesh\ngeometry step file=a.step size=big\nmaterial k=1 rhocp=1\nstep steady\n",
                  "size= is not a number"));
    CHECK(rejects("analysis heat 2d mesh\ngeometry step size=0.1\nmaterial k=1 rhocp=1\nstep steady\n",
                  "missing file="));
    CHECK(rejects("analysis heat 2d mesh\ngeometry step file=a.step size=0.1\nmaterial k=0 rhocp=1\nstep steady\n",
                  "k must be positive"));
    CHECK(rejects("analysis heat 2d mesh\ngeometry step file=a.step size=0.1\nmaterial k=1 rhocp=1 thickness=0\nstep steady\n",
                  "thickness must be positive"));
    CHECK(rejects(kValidHead + "heat q=1 extra\nstep steady\n", "line 4: unexpected word 'extra'"));
    CHECK(rejects(kValidHead + "boundary tag=1 flux T=5 q=2\nstep steady\n", "line 4: unknown key 'T'"));
    CHECK(rejects(kValidHead + "boundary tag=1 temperature T=0 q=2\nstep steady\n", "line 4: unknown key 'q'"));
    CHECK(rejects(kValidHead + "step steady dt=1\n", "line 4: unknown key 'dt'"));
    CHECK(rejects(kValidHead + "step steady\nmaterial k=2 rhocp=2\n", "line 5: material repeated"));
}

void testTokenErrors() {
    CHECK(rejects(kValidHead + "heat q\nstep steady\n", "line 4: unexpected word 'q'"));
    CHECK(rejects(kValidHead + "heat q=1 q=2\nstep steady\n", "line 4: repeated key 'q'"));
    CHECK(rejects(kValidHead + "heat q=\nstep steady\n", "line 4: malformed 'q='"));
}

void testMissingSections() {
    MeshScriptProblem p;
    std::vector<std::string> errors;
    CHECK(!parseMeshScript("", p, errors));
    CHECK(hasError(errors, "missing 'analysis heat 2d mesh'"));
    CHECK(hasError(errors, "missing 'geometry' line"));
    CHECK(hasError(errors, "missing 'material' line"));
    CHECK(hasError(errors, "missing 'step' line"));
}

void testCsv() {
    // Two nodes, two recorded times: header plus 2 x 2 rows, with the times and coordinates written as given.
    TriangleMesh mesh;
    mesh.nodes = {{0.0, 0.0}, {1.0, 0.5}};
    MeshResult result;
    result.times = {0.0, 0.25};
    result.temperatures = {{10.0, 11.0}, {12.0, 13.0}};
    const std::string csv = meshResultToCsv(mesh, result);
    CHECK(csv.rfind("time,x,y,temperature\n", 0) == 0);
    std::size_t rows = 0;
    for (char ch : csv)
        if (ch == '\n') ++rows;
    CHECK(rows == 1 + 4);
    CHECK(csv.find("0.25,1,0.5,13\n") != std::string::npos);
}

} // namespace

int main() {
    testValidTransient();
    testValidSteady();
    testDefaults();
    testErrorsWithLineNumbers();
    testTokenErrors();
    testMissingSections();
    testCsv();

    if (g_failures == 0) {
        std::cout << "heat2d_mesh_script_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_mesh_script_test: " << g_failures << " check(s) failed\n";
    return 1;
}
